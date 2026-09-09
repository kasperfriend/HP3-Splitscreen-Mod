#ifndef HP3_COOP_TRIO_H
#define HP3_COOP_TRIO_H
// Pure placement policy for the cooperative "three floating wet spells" overlay
// and for smoothing the native-aim LOCK gesture. Engine-independent on
// purpose: dllmain feeds it live positions, host tests feed it numbers.
//
// Stock evidence (docs/NATIVE_AIM.md; HP2 KWGame source, same engine family):
//   * every joined hero owns ONE SpellGesture, driven by that hero's own
//     cursor lock - the "one floating spell becomes three" look;
//   * each gesture's goal is that hero's OWN vLOS_End, i.e.
//         target.Location + CentreOffset +
//         Normal(heroLoc - targetLoc) *
//           (1.1 * CollisionRadius * SizeModifier + 2 + GestureDistance)
//     - the point at the stock gesture distance on the hero's own line of
//     sight (heroGoal below). SpellCursor.LockOn does SetLocation(vLOS_End)
//     once, and stateLockedOn.Tick then chases the goal every tick with
//     SpellGesture.MoveSmooth((goal - loc) * rate * dt): rate 10 while the
//     target is still the current possible target, rate 8 on the centre-
//     offset goal during a target flicker. The icon therefore glides in the
//     air after its hero instead of teleporting.
//   * the gesture faces opposite its owner's line of sight, i.e. from the
//     target toward that hero (facingYaw below).
//
// Because the three heroes' lines fan out from the target, three gestures
// placed with heroGoal naturally sit apart on their own heroes' sides. The
// v63/v64 "cluster" recipe - a shared front point pushed sideways by a fixed
// lateral spacing - is NOT stock and collapsed the two companions onto one
// another whenever their directions projected together; it is removed here.
#include <cmath>
#include <cstddef>
#include <cstring>

namespace hp3trio {

enum { kSmoothRate = 10, kSmoothRateFlicker = 8 };
static const float kMaxDt = 0.05f;      // clamp like driveePawn's 0.2 s, tighter

// Normalized from->to. Degenerate input yields a safe unit Z (callers that
// need real geometry test the return value).
static inline bool direction(const float from[3], const float to[3],
                             float out[3])
{
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(len > 1.0f) || len > 20000.0f) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 1.0f;
        return false;
    }
    out[0] = dx / len; out[1] = dy / len; out[2] = dz / len;
    return true;
}

// Pure stock per-hero gesture goal: the target's aim centre plus
// Normal(heroLoc - targetLoc) * dist. This is exactly the stock vLOS_End /
// centre-offset goal (target.Location + CentreOffset + Normal(hero-target) *
// fFinalGestureDistance): the icon hovers at the gesture distance on ITS OWN
// hero's line of sight, so three heroes produce three icons fanned out on
// their own sides, each following its own hero.
static inline void heroGoal(const float centre[3], const float heroLoc[3],
                            float dist, float out[3])
{
    float d[3];
    if (!direction(centre, heroLoc, d)) { d[0] = d[1] = 0.0f; d[2] = 1.0f; }
    out[0] = centre[0] + d[0] * dist;
    out[1] = centre[1] + d[1] * dist;
    out[2] = centre[2] + d[2] * dist;
}

// UE yaw whose facing vector points along dir. Used for each gesture, which
// faces opposite its owner's line of sight - i.e. from the target toward that
// hero - so dir is the target->hero unit vector.
static inline int facingYaw(const float dir[3])
{
    double y = std::atan2((double)dir[1], (double)dir[0]);
    return ((int)(y * (65536.0 / 6.283185307179586))) & 0xFFFF;
}

// MoveSmooth semantics: first sample snaps (stock LockOn SetLocation), every
// later sample moves a fraction rate*dt of the remaining distance and never
// passes the goal. Rate is the stock 10 (locked) / 8 (target flicker); dt is
// the frame delta in seconds.
static inline void moveSmooth(float pos[3], bool &started,
                              const float goal[3], float rate, float dt)
{
    if (!started) {
        std::memcpy(pos, goal, 3 * sizeof(float));
        started = true;
        return;
    }
    if (!(rate > 0.0f) || !(dt > 0.0f)) return;
    float k = rate * dt;
    if (k > 1.0f) k = 1.0f;
    for (int j = 0; j < 3; j++) pos[j] += (goal[j] - pos[j]) * k;
}

} // namespace hp3trio
#endif
