#ifndef HP3_COOP_CAST_H
#define HP3_COOP_CAST_H

// Pure hold-state policy for HP3's three-character companion-spell targets.
// It deliberately knows nothing about UE2 objects or input. The integration
// supplies a verified target identity and decides how to enter the game's
// normal cooperative-casting state once the hold matures.
#include <cstdint>

namespace hp3coop {

enum { DefaultHoldMs = 8000u };

struct Target {
    void *object;
    void *clazz;
    int slot;
};

struct Hold {
    Target target;
    std::uint32_t beganAt;
    std::uint32_t lastSeenAt;   // v57: last tick the target was still valid
    bool active;
    bool fired;
};

enum Result {
    Reset,
    Started,
    Waiting,
    Ready,
    AlreadyReady
};

static inline bool sameTarget(const Target &a, const Target &b)
{
    return a.object == b.object && a.clazz == b.clazz && a.slot == b.slot;
}

static inline void clear(Hold &hold)
{
    hold.target.object = nullptr;
    hold.target.clazz = nullptr;
    hold.target.slot = -1;
    hold.beganAt = 0;
    hold.lastSeenAt = 0;
    hold.active = false;
    hold.fired = false;
}

// The unsigned subtraction deliberately handles the normal GetTickCount()
// wraparound. A different target, a missing target, or a released cast starts
// a fresh uninterrupted interval; elapsed time is never carried across one.
//
// v57 graceMs: the holder's target can flicker for a frame or two between
// controller/aim updates without the hold being deliberately released (the
// v55/v56 hardware logs showed exactly this for the stock P1 cursor). While
// the gap since the last valid sample is <= graceMs the dwell keeps running
// against the last target; a longer gap clears as before. graceMs == 0 keeps
// the original hard-reset behaviour.
static inline Result update(Hold &hold, bool valid, const Target &target,
                            std::uint32_t now, std::uint32_t holdMs,
                            std::uint32_t graceMs = 0)
{
    if (!valid || !target.object || !target.clazz || target.slot < 0) {
        // Brief target flicker inside the grace window: keep the dwell.
        if (hold.active && !hold.fired && graceMs && hold.lastSeenAt &&
            (std::uint32_t)(now - hold.lastSeenAt) <= graceMs)
            return Waiting;
        clear(hold);
        return Reset;
    }
    hold.lastSeenAt = now;
    if (!hold.active || !sameTarget(hold.target, target)) {
        hold.target = target;
        hold.beganAt = now;
        hold.active = true;
        hold.fired = false;
        return Started;
    }
    if (hold.fired) return AlreadyReady;
    // The gameplay requirement is *more than* the configured interval, not
    // a cast sampled at its exact millisecond boundary.
    if ((std::uint32_t)(now - hold.beganAt) > holdMs) {
        hold.fired = true;
        return Ready;
    }
    return Waiting;
}

} // namespace hp3coop
#endif
