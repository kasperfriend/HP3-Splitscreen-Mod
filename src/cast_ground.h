// Small, engine-independent policy for the post-cast floor handoff.
// The engine adapter must reacquire collision support, never restore a saved Z
// or Base pointer. One attempt per exit; a failed attempt releases to physics.
#ifndef HP3_CAST_GROUND_H
#define HP3_CAST_GROUND_H

#include <stdint.h>
#include <float.h>

namespace CastGround {

enum Phase { Inactive, Armed, Repairing, Verifying, Complete, Released };
enum Action { None, Reacquire, KeepWalking, Settled, GiveUp };
enum Reason { NoReason, InvalidSample, AirOrJump, Moved, NoSupport, Timeout, Unavailable };

struct Sample {
    bool valid;
    bool jumping;
    unsigned char physics;
    float location[3];
    float vz;
    void *base;
    float floor[3];
};

struct Recovery {
    Phase phase;
    Reason reason;
    uint32_t armedAt;
    uint32_t repairAt;
    float exitLocation[3];
    unsigned int walkingFrames;
};

inline bool finite(float v) { return v >= -FLT_MAX && v <= FLT_MAX; }

inline bool readable(const Sample &s)
{
    return s.valid && finite(s.location[0]) && finite(s.location[1]) &&
           finite(s.location[2]) && finite(s.vz);
}

inline bool supported(const Sample &s)
{
    float n2 = s.floor[0] * s.floor[0] + s.floor[1] * s.floor[1] +
               s.floor[2] * s.floor[2];
    // Base alone is not enough: walking also needs a walkable floor normal.
    return readable(s) && s.base && s.floor[2] >= 0.7f &&
           n2 >= 0.9f && n2 <= 1.1f;
}

inline void release(Recovery &r, Reason why)
{
    r.phase = Released;
    r.reason = why;
}

inline bool active(const Recovery &r)
{
    return r.phase == Armed || r.phase == Repairing || r.phase == Verifying;
}

inline void arm(Recovery &r, const Sample &s, uint32_t now)
{
    Recovery fresh = {};
    r = fresh;
    if (!readable(s)) { release(r, InvalidSample); return; }
    if (s.jumping || (s.physics != 0 && s.physics != 1) ||
        s.vz < -1.0f || s.vz > 0.0f) { release(r, AirOrJump); return; }
    r.phase = Armed;
    r.armedAt = now;
    for (int a = 0; a < 3; ++a) r.exitLocation[a] = s.location[a];
}

inline Action step(Recovery &r, const Sample &s, uint32_t now)
{
    if (!active(r) || r.phase == Repairing) return None;
    if (!readable(s)) { release(r, InvalidSample); return GiveUp; }
    if (s.jumping || s.vz > 0.0f || s.vz < -80.0f || s.physics > 2) {
        release(r, AirOrJump); return GiveUp;
    }
    // Unsigned elapsed times also work across GetTickCount's wraparound.
    // This is a bound on a single handoff, NOT a window for pinning falls.
    if (uint32_t(now - r.armedAt) > 250) {
        release(r, Timeout); return GiveUp;
    }
    if (r.phase == Armed) {
        float dx = s.location[0] - r.exitLocation[0];
        float dy = s.location[1] - r.exitLocation[1];
        float dz = s.location[2] - r.exitLocation[2];
        if (!(dx * dx + dy * dy <= 32.0f * 32.0f && dz >= -4.0f && dz <= 1.0f)) {
            release(r, Moved); return GiveUp;
        }
        r.phase = Repairing; // consume the attempt BEFORE calling engine code
        r.repairAt = now;
        return Reacquire;
    }
    // Inspect what the engine left us on later frames. Never turn a recurrent
    // fall back into walking: that was v48's Falling -> Walking -> Falling loop.
    if (s.physics != 1 || !supported(s)) {
        release(r, NoSupport); return GiveUp;
    }
    ++r.walkingFrames;
    if (r.walkingFrames >= 2 && uint32_t(now - r.repairAt) >= 50) {
        r.phase = Complete;
        return Settled;
    }
    return KeepWalking;
}

inline bool repaired(Recovery &r, const Sample &s)
{
    if (r.phase != Repairing) return false;
    if (!readable(s)) { release(r, InvalidSample); return false; }
    if (s.jumping || s.vz > 0.0f || s.vz < -80.0f) {
        release(r, AirOrJump); return false;
    }
    if (s.physics != 1 || !supported(s)) { release(r, NoSupport); return false; }
    r.phase = Verifying;
    return true;
}

inline const char *phaseName(Phase phase)
{
    switch (phase) {
    case Inactive: return "off";
    case Armed: return "armed";
    case Repairing: return "repairing";
    case Verifying: return "verifying";
    case Complete: return "verified";
    case Released: return "released";
    }
    return "unknown";
}

inline const char *reasonName(Reason reason)
{
    switch (reason) {
    case NoReason: return "none";
    case InvalidSample: return "unreadable fields";
    case AirOrJump: return "air/jump";
    case Moved: return "moved from exit";
    case NoSupport: return "no stable floor contact";
    case Timeout: return "handoff timeout";
    case Unavailable: return "native floor repair unavailable";
    }
    return "unknown";
}

} // namespace CastGround
#endif
