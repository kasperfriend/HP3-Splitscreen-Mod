#include "../src/charged_cast.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b, float epsilon = 0.01f)
{
    return std::fabs(a - b) <= epsilon;
}

int main()
{
    using hp3charged::LaunchPlan;
    using hp3charged::Vec3;

    // A v59 SpawnSpell result at the caster is moved clear and receives a
    // full-speed trajectory toward the cooperative target.
    Vec3 origin = { 0.0f, 0.0f, 0.0f };
    Vec3 target = { 300.0f, 400.0f, 0.0f };
    LaunchPlan plan = hp3charged::planTargetedLaunch(origin, target, 700.0f);
    assert(plan.valid);
    assert(near(plan.location.x, 54.0f));
    assert(near(plan.location.y, 72.0f));
    assert(near(plan.location.z, 0.0f));
    assert(near(plan.velocity.x, 420.0f));
    assert(near(plan.velocity.y, 560.0f));
    assert(near(plan.velocity.z, 0.0f));
    assert(near(std::sqrt(plan.velocity.x * plan.velocity.x +
                          plan.velocity.y * plan.velocity.y +
                          plan.velocity.z * plan.velocity.z), 700.0f));

    // Close targets are not overshot by the clearance nudge.
    target = { 30.0f, 0.0f, 0.0f };
    plan = hp3charged::planTargetedLaunch(origin, target, 1200.0f);
    assert(plan.valid);
    assert(near(plan.location.x, 15.0f));
    assert(near(plan.velocity.x, 1200.0f));

    // Vertical targets and non-zero origins use the same normalized path.
    origin = { 10.0f, -20.0f, 50.0f };
    target = { 10.0f, -20.0f, 250.0f };
    plan = hp3charged::planTargetedLaunch(origin, target, 800.0f);
    assert(plan.valid);
    assert(near(plan.location.z, 140.0f));
    assert(near(plan.velocity.z, 800.0f));

    // A missing direction or invalid speed must not manufacture NaNs or a
    // projectile that remains falsely marked as launchable.
    plan = hp3charged::planTargetedLaunch(origin, origin, 700.0f);
    assert(!plan.valid);
    plan = hp3charged::planTargetedLaunch(origin, target, 0.0f);
    assert(!plan.valid);

    puts("charged-cast targeted projectile planning assertions passed");
}
