#include "../src/stuck_pawn.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace hp3stuck;

static Sample mk(float x, float y, float z, float vz, unsigned char phys)
{
    Sample s;
    s.valid = true;
    s.location[0] = x; s.location[1] = y; s.location[2] = z;
    s.vz = vz;
    s.physics = phys;
    return s;
}

// Place the lead character exactly `dist` units from the sample's position and
// keep leadDist consistent with leadPos. The policy derives the rescue range
// from leadPos, so a test that set leadDist alone would be lying to itself.
static void setLead(Sample &s, float dist)
{
    s.haveLead = true;
    s.leadDist = dist;
    s.leadPos[0] = s.location[0] + dist;
    s.leadPos[1] = s.location[1];
    s.leadPos[2] = s.location[2];
}

// Drive the policy forward one wedge window at a time and collect the actions
// of ONE escalation cycle (up to and including the first GiveUp).
static std::vector<Action> oneCycle(State &st, const Sample &s, const Config &c,
                                    uint32_t &t)
{
    std::vector<Action> out;
    for (int i = 0; i < 64; ++i) {
        t += c.wedgeMs;
        Action a = step(st, s, c, t);
        if (a == None) continue;
        out.push_back(a);
        if (a == GiveUp) break;
    }
    return out;
}

static int countOf(const std::vector<Action> &v, Action a)
{
    int n = 0;
    for (size_t i = 0; i < v.size(); ++i) if (v[i] == a) ++n;
    return n;
}

int main()
{
    Config c;

    // 1. A healthy walking pawn never triggers anything, even over a long
    //    window: it keeps making progress, so the window keeps re-anchoring.
    {
        State st;
        for (uint32_t t = 0; t < 5000; t += 100) {
            Sample s = mk(0.0f, (float)t * 0.5f, 137.0f, 0.0f, 1);
            assert(step(st, s, c, t) == None);
        }
        assert(st.lifts == 0 && st.rescues == 0);
        assert(st.phase == Idle);
    }

    // 2. A genuine long fall (real vertical velocity) is left alone: this is
    //    the case v13's airborne veto and the cast-ground handoff rely on.
    {
        State st;
        for (uint32_t t = 0; t < 4000; t += 50) {
            Sample s = mk(0.0f, 0.0f, 2000.0f - (float)t, -900.0f, 2);
            assert(step(st, s, c, t) == None);
        }
        assert(st.lifts == 0);
    }

    // 3. The exact field failure: PHYS_Falling, vz == 0, Z frozen at 137.
    //    The first sample only anchors; nothing fires inside the grace
    //    window; then the ladder starts.
    {
        State st;
        Sample wedged = mk(1004.0f, 1050.0f, 137.0f, 0.0f, 2);
        assert(step(st, wedged, c, 0) == None);
        assert(st.phase == Watching);
        assert(step(st, wedged, c, 400) == None);      // inside wedgeMs
        assert(step(st, wedged, c, 899) == None);
        assert(step(st, wedged, c, 900) == Lift);      // first escalation
        assert(st.lifts == 1 && st.phase == Lifting);
    }

    // 4. One escalation cycle is bounded: three lifts, then rescue, then back
    //    off. Lead is 119 units away, exactly as the HP3_InsideHub log shows.
    {
        State st;
        Sample wedged = mk(1004.0f, 1050.0f, 137.0f, 0.0f, 2);
        setLead(wedged, 119.0f);
        uint32_t t = 0;
        std::vector<Action> acts = oneCycle(st, wedged, c, t);
        assert(countOf(acts, Lift) == kMaxLifts);
        assert(countOf(acts, Rescue) == (int)c.maxRescues);
        assert(countOf(acts, GiveUp) == 1);
        assert(acts.back() == GiveUp);
        assert(st.phase == Cooldown);
        // Order is lifts first, then rescues, then give up.
        assert(acts[0] == Lift && acts[kMaxLifts] == Rescue);
    }

    // 5. During the cooldown no repair is issued at all; once it expires the
    //    ladder is available again for a NEW wedge.
    {
        State st;
        Sample wedged = mk(10.0f, 10.0f, 137.0f, 0.0f, 2);
        uint32_t t = 0;
        oneCycle(st, wedged, c, t);
        assert(st.phase == Cooldown);
        uint32_t cdEnd = st.cooldownUntil;
        assert(step(st, wedged, c, cdEnd - 1) == None);
        assert(st.lifts == kMaxLifts);                 // not reset mid-cooldown
        // Still wedged after the cooldown: the ladder restarts from zero, but
        // a fresh no-progress window is measured first (the anchor was
        // dropped with the cooldown, so the next sample only re-anchors).
        assert(step(st, wedged, c, cdEnd + c.wedgeMs) == None);
        assert(st.lifts == 0);
        assert(step(st, wedged, c, cdEnd + 2 * c.wedgeMs) == Lift);
        assert(st.lifts == 1);
    }

    // 6. No lead character available -> the ladder stops at GiveUp instead of
    //    inventing a rescue destination.
    {
        State st;
        Sample wedged = mk(0.0f, 0.0f, 137.0f, 0.0f, 2);
        wedged.haveLead = false;
        uint32_t t = 0;
        std::vector<Action> acts = oneCycle(st, wedged, c, t);
        assert(countOf(acts, Rescue) == 0);
        assert(countOf(acts, Lift) == kMaxLifts);
    }

    // 7. A lead that is too far away is not a rescue target either.
    {
        State st;
        Sample wedged = mk(0.0f, 0.0f, 137.0f, 0.0f, 2);
        setLead(wedged, c.rescueMaxDist + 1.0f);
        uint32_t t = 0;
        std::vector<Action> acts = oneCycle(st, wedged, c, t);
        assert(countOf(acts, Rescue) == 0);
    }

    // 8. THE SOFT-LOCK FROM THE LOG: the standing-jump latch is set, the pawn
    //    never leaves PHYS_Falling and never moves, so the jump code never
    //    clears the latch and every later jump is refused ("jump ignored
    //    (airborne, phys=2)"). The watchdog must clear it.
    {
        State st;
        Sample latched = mk(1004.0f, 1050.0f, 137.0f, 0.0f, 2);
        latched.jumpLatched = true;
        assert(step(st, latched, c, 0) == None);        // first sample anchors
        assert(st.jumpSeen && st.jumpAt == 0);          // t==0 is not "unset"
        assert(step(st, latched, c, c.wedgeMs - 1) == None);
        assert(step(st, latched, c, c.wedgeMs) == ClearJumpLatch);
        // Cleared once; it does not re-fire every frame.
        assert(step(st, latched, c, c.wedgeMs + 1) == None);
    }

    // 8b. A latch that leaks while the player is still WALKING never produces
    //     a wedge window, so only the absolute flight bound can clear it.
    {
        State st;
        uint32_t t = 0;
        for (; t < c.jumpMaxFlightMs; t += 100) {
            Sample walking = mk((float)t * 0.5f, 0.0f, 137.0f, 0.0f, 2);
            walking.jumpLatched = true;
            assert(step(st, walking, c, t) == None);
        }
        Sample walking = mk((float)t * 0.5f, 0.0f, 137.0f, 0.0f, 2);
        walking.jumpLatched = true;
        assert(step(st, walking, c, t) == ClearJumpLatch);
    }

    // 9. A real jump in flight is never touched: it has real vertical
    //    velocity, so it is not a wedge candidate and the latch stays set.
    {
        State st;
        Sample jumping = mk(0.0f, 0.0f, 900.0f, 320.0f, 2);
        jumping.jumpLatched = true;
        for (uint32_t t = 0; t < c.jumpMaxFlightMs; t += 50)
            assert(step(st, jumping, c, t) == None);
        assert(st.jumpSeen);
    }

    // 9b. A real jump's apex (|vz| briefly near zero while airborne) is far
    //     too short to read as wedged, and the pawn is still moving.
    {
        State st;
        for (uint32_t t = 0; t < 600; t += 50) {
            float vz = 320.0f - (float)t * 1.2f;        // passes through ~0
            Sample apex = mk(0.0f, (float)t * 2.0f, 900.0f + (float)t, vz, 2);
            apex.jumpLatched = true;
            assert(step(st, apex, c, t) == None);
        }
        assert(st.lifts == 0);
    }

    // 10. Once the latch is gone the wedge timer restarts clean, so the
    //     ladder is judged from the post-clear position.
    {
        State st;
        Sample latched = mk(5.0f, 5.0f, 137.0f, 0.0f, 2);
        latched.jumpLatched = true;
        assert(step(st, latched, c, 0) == None);
        assert(step(st, latched, c, c.wedgeMs) == ClearJumpLatch);
        assert(st.watchAt == c.wedgeMs && st.watching);
        latched.jumpLatched = false;
        assert(step(st, latched, c, 2 * c.wedgeMs - 1) == None);
        assert(step(st, latched, c, 2 * c.wedgeMs) == Lift);
    }

    // 11. Real progress resets the ladder: a lift that worked must not be
    //     followed by more lifts.
    {
        State st;
        Sample wedged = mk(0.0f, 0.0f, 137.0f, 0.0f, 2);
        uint32_t t = 0;
        for (int i = 0; i < 4; ++i) { t += c.wedgeMs; step(st, wedged, c, t); }
        assert(st.lifts == kMaxLifts);
        // The pawn walked free.
        for (int i = 0; i < 5; ++i) {
            t += 100;
            Sample ok = mk((float)i * 40.0f, 0.0f, 137.0f, 0.0f, 1);
            assert(step(st, ok, c, t) == None);
        }
        assert(st.lifts == 0 && st.phase == Idle);
        // Wedged again somewhere else: a full ladder is available. The first
        // sample at the new spot only re-anchors.
        Sample again = mk(900.0f, 900.0f, 300.0f, 0.0f, 2);
        t += c.wedgeMs;
        assert(step(st, again, c, t) == None);
        t += c.wedgeMs;
        assert(step(st, again, c, t) == Lift);
    }

    // 12. Level travel / cutscene suppresses everything and clears the
    //     positional state, but a running cooldown survives it.
    {
        State st;
        Sample wedged = mk(0.0f, 0.0f, 137.0f, 0.0f, 2);
        uint32_t t = 0;
        oneCycle(st, wedged, c, t);
        uint32_t cdEnd = st.cooldownUntil;
        assert(cdEnd != 0);
        Sample travelling = wedged;
        travelling.blocked = true;
        assert(step(st, travelling, c, t + 10) == None);
        assert(st.phase == Idle && st.haveAnchor == false && st.watching == false);
        assert(st.cooldownUntil == cdEnd);             // cooldown preserved
        // The cooldown is still honoured after travel ends.
        Sample after = wedged;
        assert(step(st, after, c, cdEnd - 1) == None);
    }

    // 13. Unreadable samples (a level change mid-frame) must not act.
    {
        State st;
        Sample bad;
        bad.valid = false;
        bad.physics = 2;
        assert(step(st, bad, c, 5000) == None);
        Sample nan = mk(0.0f, 0.0f, 0.0f, 0.0f, 2);
        nan.location[0] = FLT_MAX * 2.0f;   // overflow -> not finite
        assert(!readable(nan));
        assert(step(st, nan, c, 6000) == None);
    }

    // 14. GetTickCount wraparound: a wedge window that spans the 32-bit
    //     rollover must still fire, not stall forever.
    {
        State st;
        Sample wedged = mk(0.0f, 0.0f, 137.0f, 0.0f, 2);
        uint32_t t = 0xFFFFFF00u;
        assert(step(st, wedged, c, t) == None);
        assert(step(st, wedged, c, (uint32_t)(t + c.wedgeMs - 1)) == None);
        assert(step(st, wedged, c, (uint32_t)(t + c.wedgeMs)) == Lift);
    }

    // 15. Helpers used by the log lines.
    assert(std::string(phaseName(Cooldown)) == "cooldown");
    assert(std::string(phaseName(Watching)) == "watching");
    assert(std::string(actionName(ClearJumpLatch)) == "clear-jump-latch");
    assert(std::string(actionName(Lift)) == "lift");
    assert(std::string(actionName(Rescue)) == "rescue");
    assert(std::string(actionName(GiveUp)) == "give-up");
    assert(std::string(actionName(None)) == "none");

    std::puts("stuck pawn: wedge detection, latch watchdog, bounded escalation "
              "and wraparound assertions passed");
    return 0;
}
