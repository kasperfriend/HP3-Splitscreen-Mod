#ifndef HP3_COOP_CAST_H
#define HP3_COOP_CAST_H

// Pure hold-state policy for HP3's three-character companion-spell targets.
// It deliberately knows nothing about UE2 objects or input. The integration
// supplies a verified target identity and decides how to enter the game's
// normal cooperative-casting state once the hold matures.
#include <cstdint>

namespace hp3coop {

enum { DefaultHoldMs = 10000u };

struct Target {
    void *object;
    void *clazz;
    int slot;
};

struct Hold {
    Target target;
    std::uint32_t beganAt;
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
    hold.active = false;
    hold.fired = false;
}

// The unsigned subtraction deliberately handles the normal GetTickCount()
// wraparound. A different target, a missing target, or a released cast starts
// a fresh uninterrupted interval; elapsed time is never carried across one.
static inline Result update(Hold &hold, bool valid, const Target &target,
                            std::uint32_t now, std::uint32_t holdMs)
{
    if (!valid || !target.object || !target.clazz || target.slot < 0) {
        clear(hold);
        return Reset;
    }
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
