#ifndef HP3_CHARGED_CAST_H
#define HP3_CHARGED_CAST_H

// Pure fallback trajectory planner for charged cooperative casts. P2/P3 bonus
// spells normally copy the holder's real projectile kinematics exactly. The
// stock P1 projectile is engine-owned, so its first bonus uses this planner at
// the holder's position and the second bonus clones that result. SpawnSpell can
// otherwise leave an actor at the pawn with zero velocity (the v59 red glow).
#include <cmath>

namespace hp3charged {

struct Vec3 {
    float x, y, z;
};

struct LaunchPlan {
    Vec3 location;
    Vec3 velocity;
    bool valid;
};

static inline LaunchPlan planTargetedLaunch(const Vec3 &origin,
                                             const Vec3 &target,
                                             float speed,
                                             float desiredClearance = 90.0f)
{
    LaunchPlan plan = { origin, { 0.0f, 0.0f, 0.0f }, false };
    const float dx = target.x - origin.x;
    const float dy = target.y - origin.y;
    const float dz = target.z - origin.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(distance > 1.0f) || !(speed > 0.0f)) return plan;

    const float invDistance = 1.0f / distance;
    const Vec3 direction = { dx * invDistance, dy * invDistance,
                             dz * invDistance };
    float clearance = desiredClearance;
    if (clearance < 0.0f) clearance = 0.0f;
    // Never put a close-range shot beyond its target. Half the remaining
    // distance still gets it away from the caster while preserving a flight.
    if (clearance > distance * 0.5f) clearance = distance * 0.5f;

    plan.location.x = origin.x + direction.x * clearance;
    plan.location.y = origin.y + direction.y * clearance;
    plan.location.z = origin.z + direction.z * clearance;
    plan.velocity.x = direction.x * speed;
    plan.velocity.y = direction.y * speed;
    plan.velocity.z = direction.z * speed;
    plan.valid = true;
    return plan;
}

} // namespace hp3charged
#endif
