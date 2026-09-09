#ifndef HP3_COOP_TRIO_H
#define HP3_COOP_TRIO_H
// Pure placement policy for the cooperative "three floating wet spells" overlay
// and for smoothing the native-aim LOCK gesture. Engine-independent on
// purpose: dllmain feeds it live positions, host tests feed it numbers.
//
// Stock evidence (docs/NATIVE_AIM.md; HP2 KWGame source, same engine family):
//   * every joined hero owns one SpellGesture;
//   * the gesture's goal sits at
//        1.1 * CollisionRadius * SizeModifier + 2 + GestureDistance
//     from the target's aim centre, on the line of the gesture's owner -
//     SpellCursor.LockOn does SetLocation(vLOS_End) once (the camera-ray end
//     in front of the target) and stateLockedOn.Tick then chases the goal
//     with SpellGesture.MoveSmooth((goal - loc) * rate * dt), rate 10 while
//     the target is still a possible target, rate 8 on the centre-offset
//     goal during a target flicker. The icon therefore glides in the air
//     after the aim/its hero instead of teleporting.
// The overlay renders in the holder's pane: each companion goal is the stock
// front-of-target point (centre - vLOS_Dir * dist) pushed sideways along that
// hero's own direction, so the three icons cluster in front of the target and
// each one follows its hero - the exact stock 1-to-3 look when the heroes
// stand together, and still visible when a companion is far away.
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

// Pure stock hero-gesture goal: target aim centre + Normal(heroLoc-targetLoc)
// * dist. Used verbatim when the hero is on the viewer's side (single-player
// and the normal coop arrangement).
static inline void heroGoal(const float centre[3], const float heroLoc[3],
                            float dist, float out[3])
{
    float d[3];
    if (!direction(centre, heroLoc, d)) { d[0] = d[1] = 0.0f; d[2] = 1.0f; }
    out[0] = centre[0] + d[0] * dist;
    out[1] = centre[1] + d[1] * dist;
    out[2] = centre[2] + d[2] * dist;
}

// Stock front-of-target point as seen by the pane's viewer: the camera-ray
// end at the gesture distance, the point LockOn snaps the gesture to.
static inline void frontPoint(const float centre[3], const float viewDir[3],
                              float dist, float out[3])
{
    out[0] = centre[0] - viewDir[0] * dist;
    out[1] = centre[1] - viewDir[1] * dist;
    out[2] = centre[2] - viewDir[2] * dist;
}

// Companion goal used by the split-screen overlay: the stock front point
// pushed sideways along the hero's own direction (projected perpendicular to
// the view), so the icon hovers in front of the target on its hero's side.
// heroDir is the unit target->hero vector; a zero/degenerate projection
// leaves the goal exactly on the front point (that hero is on the view axis).
static inline void clusterGoal(const float centre[3], const float viewDir[3],
                               const float heroDir[3], float dist,
                               float spacing, float out[3])
{
    float dot = heroDir[0] * viewDir[0] + heroDir[1] * viewDir[1] +
                heroDir[2] * viewDir[2];
    float lx = heroDir[0] - viewDir[0] * dot;
    float ly = heroDir[1] - viewDir[1] * dot;
    float lz = heroDir[2] - viewDir[2] * dot;
    float len = std::sqrt(lx * lx + ly * ly + lz * lz);
    float s = 0.0f;
    if (len > 0.05f) { lx /= len; ly /= len; lz /= len; s = spacing; }
    out[0] = centre[0] - viewDir[0] * dist + lx * s;
    out[1] = centre[1] - viewDir[1] * dist + ly * s;
    out[2] = centre[2] - viewDir[2] * dist + lz * s;
}

// UE yaw whose facing vector points along dir (used for the gesture, which
// faces opposite its owner's line of sight, i.e. from the target toward that
// hero).
static inline int facingYaw(const float dir[3])
{
    double y = std::atan2((double)dir[1], (double)dir[0]);
    return ((int)(y * (65536.0 / 6.283185307179586))) & 0xFFFF;
}

// MoveSmooth semantics: first sample snaps (stock LockOn SetLocation), every
// later sample moves a fraction rate*dt of the remaining distance and never
// passes the goal. Rate is the stock 10/8; dt is the frame delta in seconds.
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

// Lateral fallback used only when a companion pawn is unavailable: keep the
// icon beside the base point (the stock front point) in the holder's view
// plane so the three-icon look survives. Same recipe as the v63 overlay.
static inline void lateralGoal(const float base[3], const float holderLoc[3],
                               float radius, int side, float out[3])
{
    float hx = base[0] - holderLoc[0], hy = base[1] - holderLoc[1];
    float hl = std::sqrt(hx * hx + hy * hy);
    if (hl < 1.0f) { hx = 1.0f; hy = 0.0f; hl = 1.0f; }
    float ux = -hy / hl, uy = hx / hl;
    float spacing = radius * 0.9f + 25.0f;
    if (spacing < 50.0f) spacing = 50.0f;
    if (spacing > 120.0f) spacing = 120.0f;
    float s = (side == 0) ? spacing : -spacing;
    out[0] = base[0] + ux * s;
    out[1] = base[1] + uy * s;
    out[2] = base[2] + 6.0f;
}

} // namespace hp3trio
#endif
