#ifndef HP3_CHARGED_CAST_H
#define HP3_CHARGED_CAST_H

// Pure trajectory planner for the two same-caster projectiles added by a
// charged cooperative cast. SpawnSpell can return a projectile at the pawn's
// origin with zero velocity; leaving it there creates the permanent red glow
// seen in the v59 hardware report. The runtime applies this plan to each bonus
// actor before the next engine tick.
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
