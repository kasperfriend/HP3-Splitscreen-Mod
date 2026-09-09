#ifndef HP3_STUCK_PAWN_H
#define HP3_STUCK_PAWN_H

// Recovery policy for a DRIVEN companion that has stopped making progress.
//
// Two distinct failures were observed entering Hogwarts (HP3_InsideHub) and
// both end in the same soft-lock:
//
//  1. The door transition parks the pawn in PHYS_Falling with no real fall
//     (flat Z, vz == 0). v66.1 hands PHYS_Walking back through the native, but
//     the engine finds no floor at that spot and drops it straight back to
//     Falling, so the same repair repeats forever without ever freeing the
//     pawn.
//  2. Worse, if the player jumps while that is happening, the standing-jump
//     state machine only clears its latch once the pawn LEAVES PHYS_Falling.
//     A pawn that never lands therefore keeps the latch set for the rest of
//     the level, which permanently refuses every later jump ("jump ignored
//     (airborne, phys=2)") and permanently disables the stranded-fall repair,
//     which is gated on the latch being clear. That is the soft-lock.
//
// This policy owns the decision of WHEN to act and WHAT to try next; the
// engine adapter performs the actual SetLocation / FindBase / SetPhysics work
// and reports back. Escalation is bounded and backs off, so it can never fight
// a cutscene or a genuine long fall indefinitely.
//
// Pure policy: no engine types, so it is host-testable.

#include <stdint.h>
#include <cmath>
#include <float.h>

namespace hp3stuck {

enum Phase { Idle, Watching, Lifting, Rescuing, Cooldown, GivenUp };
enum Action { None, ClearJumpLatch, Lift, Rescue, GiveUp };

enum { kMaxLifts = 3 };

struct Config {
    // A jump may legitimately stay airborne this long (a long ledge fall).
    // Past it, with the pawn still in PHYS_Falling, the latch is a leak.
    uint32_t jumpMaxFlightMs;
    // No measurable movement for this long while "falling" == wedged.
    uint32_t wedgeMs;
    // Smallest motion that counts as progress, in Unreal units.
    float moveEps;
    // How far the adapter raises the pawn per lift attempt.
    float liftHeight;
    // Back off this long after exhausting the ladder.
    uint32_t cooldownMs;
    // Rescue (re-seat next to the lead character) only inside this distance.
    float rescueMaxDist;
    unsigned maxRescues;

    Config()
        : jumpMaxFlightMs(2500)
        , wedgeMs(900)
        , moveEps(4.0f)
        , liftHeight(48.0f)
        , cooldownMs(4000)
        , rescueMaxDist(2500.0f)
        , maxRescues(2)
    {}
};

struct Sample {
    bool valid;
    unsigned char physics;   // 2 == PHYS_Falling
    float location[3];
    float vz;
    bool jumpLatched;        // the mod's own standing-jump latch is set
    bool haveLead;           // a lead character position is available
    float leadDist;          // distance to it, when haveLead
    float leadPos[3];        // where it is, when haveLead: the rescue target
    bool blocked;            // level travel / cutscene: never act

    Sample()
        : valid(false)
        , physics(0)
        , vz(0.0f)
        , jumpLatched(false)
        , haveLead(false)
        , leadDist(0.0f)
        , blocked(false)
    {
        location[0] = location[1] = location[2] = 0.0f;
        leadPos[0] = leadPos[1] = leadPos[2] = 0.0f;
    }
};

struct State {
    Phase phase;
    uint32_t watchAt;        // when the current no-progress window started
    uint32_t jumpAt;         // when the latch was first seen set
    uint32_t actAt;          // when the last repair was issued
    uint32_t cooldownUntil;
    float anchor[3];         // position the no-progress window is measured from
    bool haveAnchor;
    // Timestamps are paired with explicit "is set" flags: a timestamp of 0 is
    // a legal GetTickCount value, so 0 cannot double as "unset".
    bool watching;
    bool jumpSeen;
    unsigned lifts;
    unsigned rescues;

    State()
        : phase(Idle)
        , watchAt(0)
        , jumpAt(0)
        , actAt(0)
        , cooldownUntil(0)
        , haveAnchor(false)
        , watching(false)
        , jumpSeen(false)
        , lifts(0)
        , rescues(0)
    {
        anchor[0] = anchor[1] = anchor[2] = 0.0f;
    }
};

inline bool finite(float v) { return v >= -FLT_MAX && v <= FLT_MAX; }

inline bool readable(const Sample &s)
{
    return s.valid && finite(s.location[0]) && finite(s.location[1]) &&
           finite(s.location[2]) && finite(s.vz);
}

inline bool airborne(const Sample &s) { return s.physics == 2; }

// "No real fall": in PHYS_Falling but with no meaningful vertical velocity.
// A genuine jump or ledge fall always has |vz| well above this.
inline bool noRealFall(const Sample &s) { return s.vz > -60.0f && s.vz < 60.0f; }

inline float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    float d2 = dx * dx + dy * dy + dz * dz;
    return d2 > 0.0f ? std::sqrt(d2) : 0.0f;
}

inline bool moved(const State &st, const Sample &s, const Config &c)
{
    if (!st.haveAnchor) return true;
    return distance(st.anchor, s.location) > c.moveEps;
}

inline void reanchor(State &st, const Sample &s)
{
    for (int a = 0; a < 3; ++a) st.anchor[a] = s.location[a];
    st.haveAnchor = true;
}

inline void reset(State &st)
{
    st.phase = Idle;
    st.watchAt = 0;
    st.actAt = 0;
    st.haveAnchor = false;
    st.watching = false;
    st.lifts = 0;
    st.rescues = 0;
    // jumpSeen/jumpAt and cooldownUntil are deliberately kept: they are
    // cross-episode memory (a latch already in flight must not re-read as
    // brand new, and a cooldown must survive a period of normal walking).
}

// Returns the action the adapter must perform this frame, or None.
//
// Order matters: the latch leak is cleared first because it is free, has no
// positional side effect, and is what re-enables both jumping and the
// engine's own recovery. Only then does the positional ladder run.
inline Action step(State &st, const Sample &s, const Config &c, uint32_t now)
{
    if (!readable(s)) { reset(st); return None; }

    // Level travel / cutscene: forget everything positional but keep the
    // cooldown so we do not act the instant travel ends.
    if (s.blocked) { reset(st); st.jumpSeen = false; st.jumpAt = 0; return None; }

    if (st.phase == Cooldown) {
        if ((int32_t)(now - st.cooldownUntil) < 0) return None;
        st.phase = Idle;
        st.lifts = 0;
        st.rescues = 0;
        st.haveAnchor = false;
        st.watching = false;
    }
    if (st.phase == GivenUp) { st.phase = Idle; st.haveAnchor = false; st.watching = false; }

    // ---- 1. latch bookkeeping, independent of movement ----------------
    // A leaked latch has to be detected while the player is walking too, so
    // this runs before any positional early-out.
    if (s.jumpLatched) {
        if (!st.jumpSeen) { st.jumpSeen = true; st.jumpAt = now; }
    } else {
        st.jumpSeen = false;
        st.jumpAt = 0;
    }

    // ---- 2. how long has the pawn been making no progress? ------------
    // "Falling with no real fall" is the wedged signature. Walking or a
    // genuine fall simply re-anchors the window.
    bool candidate = airborne(s) && noRealFall(s);
    bool wedged = false;
    if (!candidate || moved(st, s, c)) {
        // Progress ends the episode: a pawn that walked free and later wedges
        // somewhere else deserves a full ladder again. reset() deliberately
        // keeps the latch memory and any running cooldown.
        reset(st);
        reanchor(st, s);
        st.watchAt = now;
        st.watching = true;
        st.phase = candidate ? Watching : Idle;
    } else {
        if (!st.watching) {
            st.watchAt = now;
            st.watching = true;
            reanchor(st, s);
            st.phase = Watching;
        }
        wedged = uint32_t(now - st.watchAt) >= c.wedgeMs;
    }

    // ---- 3. clear a leaked latch before anything positional -----------
    // Two independent proofs that the standing-jump latch has leaked:
    //   (a) it has outlived any possible jump, or
    //   (b) the pawn is provably not jumping, because it has made no progress
    //       at all for a whole wedge window while the latch was held.
    // (b) fires first in the field case. A real jump cannot satisfy it: |vz|
    // stays under 60 for only a frame or two at the apex, and the pawn moves
    // more than moveEps in that time.
    // Clearing the latch is free and moves nothing, so it goes first: it is
    // what re-enables jump input, the engine's own recovery and the ladder.
    if (s.jumpLatched) {
        bool stale = uint32_t(now - st.jumpAt) >= c.jumpMaxFlightMs;
        if (stale || wedged) {
            st.jumpSeen = false;
            st.jumpAt = 0;
            // The latch was holding the pawn in place, so any window measured
            // while it was set measured a false stall. Start over from here.
            reanchor(st, s);
            st.watchAt = now;
            st.watching = true;
            st.phase = Watching;
            return ClearJumpLatch;
        }
    }

    if (!wedged) return None;

    // ---- 4. escalate --------------------------------------------------
    // One repair per wedge window; the window restarts so the next attempt is
    // judged on the position the repair actually produced.
    st.watchAt = now;
    st.actAt = now;

    if (st.lifts < kMaxLifts) {
        ++st.lifts;
        reanchor(st, s);
        st.phase = Lifting;
        return Lift;
    }
    if (st.rescues < c.maxRescues && s.haveLead &&
        distance(s.location, s.leadPos) <= c.rescueMaxDist) {
        ++st.rescues;
        reanchor(st, s);
        st.phase = Rescuing;
        return Rescue;
    }

    st.phase = Cooldown;
    st.cooldownUntil = now + c.cooldownMs;
    return GiveUp;
}

inline const char *phaseName(Phase p)
{
    switch (p) {
    case Idle:     return "idle";
    case Watching: return "watching";
    case Lifting:  return "lifting";
    case Rescuing: return "rescuing";
    case Cooldown: return "cooldown";
    case GivenUp:  return "given-up";
    }
    return "unknown";
}

inline const char *actionName(Action a)
{
    switch (a) {
    case None:           return "none";
    case ClearJumpLatch: return "clear-jump-latch";
    case Lift:           return "lift";
    case Rescue:         return "rescue";
    case GiveUp:         return "give-up";
    }
    return "unknown";
}

} // namespace hp3stuck

#endif // HP3_STUCK_PAWN_H
