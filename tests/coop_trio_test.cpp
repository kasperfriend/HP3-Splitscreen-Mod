// Host-side tests for the cooperative "three floating spells" placement
// policy: stock hero-goal geometry, front-of-target cluster goals, MoveSmooth
// chase semantics, facing yaw, and the lateral fallback. No engine ABI.
#include "../src/coop_trio.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool close3(const float a[3], const float b[3], float eps)
{
    return std::fabs(a[0] - b[0]) <= eps &&
           std::fabs(a[1] - b[1]) <= eps &&
           std::fabs(a[2] - b[2]) <= eps;
}

int main()
{
    const float centre[3] = { 100, 200, 30 };
    const float dist = 1.1f * 60.0f * 1.0f + 2.0f + 0.0f; // 68

    // Pure stock hero goal: target aim centre + Normal(heroLoc-targetLoc)*dist
    // (target.Location + CentreOffset + Normal(hero-target)*fFinalGestureDistance).
    {
        const float hero[3] = { 400, 200, 30 };
        float g[3], expect[3] = { 100 + 68, 200, 30 };
        hp3trio::heroGoal(centre, hero, dist, g);
        assert(close3(g, expect, 0.001f));
    }
    {
        const float hero[3] = { 100, 0, 30 };
        float g[3], expect[3] = { 100, 200 - 68, 30 };
        hp3trio::heroGoal(centre, hero, dist, g);
        assert(close3(g, expect, 0.001f));
    }
    // Degenerate hero position (same as target) must not blow up.
    {
        const float hero[3] = { 100, 200, 30 };
        float g[3];
        hp3trio::heroGoal(centre, hero, dist, g);
        for (int j = 0; j < 3; j++) assert(std::isfinite(g[j]));
    }

    // Front point: the stock vLOS_End - dist along the camera ray in front of
    // the target (LockOn snaps the gesture there).
    {
        const float view[3] = { 1, 0, 0 };
        float f[3], expect[3] = { 100 - 68, 200, 30 };
        hp3trio::frontPoint(centre, view, dist, f);
        assert(close3(f, expect, 0.001f));
    }

    // Cluster goal: front point pushed sideways along the hero's direction
    // projected perpendicular to the view. Hero due +Y of the target while
    // looking along +X -> icon +spacing on Y; hero on the view axis -> icon
    // exactly at the front point.
    {
        const float view[3] = { 1, 0, 0 };
        const float hero[3] = { 100, 400, 30 };
        float dirH[3];
        assert(hp3trio::direction(centre, hero, dirH));
        float g[3], expect[3] = { 100 - 68, 200 + 79, 30 };
        hp3trio::clusterGoal(centre, view, dirH, dist, 79, g);
        assert(close3(g, expect, 0.01f));
        // Hero on the view axis: no lateral displacement.
        float dirX[3] = { 1, 0, 0 };
        hp3trio::clusterGoal(centre, view, dirX, dist, 79, g);
        float expectFront[3] = { 100 - 68, 200, 30 };
        assert(close3(g, expectFront, 0.01f));
    }

    // Facing: the gesture faces opposite its owner's line of sight, i.e. from
    // the target toward the hero/viewer. Hero at +X -> yaw 0; +Y -> 16384
    // (90 deg); -X -> 32768.
    {
        float px[3] = { 1, 0, 0 }, py[3] = { 0, 1, 0 }, nx[3] = { -1, 0, 0 };
        assert(hp3trio::facingYaw(px) == 0);
        assert(hp3trio::facingYaw(py) == 16384);
        assert(hp3trio::facingYaw(nx) == 32768);
    }

    // MoveSmooth: the first sample snaps (stock LockOn.SetLocation), the next
    // samples move 10*dt of the remaining distance and converge.
    {
        const float from[3] = { 0, 0, 0 };
        const float to[3]   = { 100, 0, 0 };
        float pos[3]; std::memcpy(pos, from, sizeof(pos));
        bool started = false;
        hp3trio::moveSmooth(pos, started, to, 10.0f, 0.016f);
        assert(started && close3(pos, to, 0.001f));     // snap at lock
        hp3trio::moveSmooth(pos, started, to, 10.0f, 0.016f);
        assert(close3(pos, to, 0.001f));                // already there
        // Move the goal: one 16 ms frame moves 16%, never overshoots.
        float g2[3] = { 200, 0, 0 };
        hp3trio::moveSmooth(pos, started, g2, 10.0f, 0.016f);
        assert(pos[0] > 100.0f && pos[0] < 200.0f);
        assert(std::fabs(pos[0] - (100.0f + 100.0f * 0.16f)) < 0.01f);
        // A zero/negative dt is a no-op (stock would pass the frame delta).
        float before = pos[0];
        hp3trio::moveSmooth(pos, started, g2, 10.0f, 0.0f);
        assert(pos[0] == before);
        // Converge after enough frames.
        for (int k = 0; k < 200; k++)
            hp3trio::moveSmooth(pos, started, g2, 10.0f, 0.05f);
        assert(close3(pos, g2, 0.01f));
    }

    // Lateral fallback: both sides put the icon beside the base point (the
    // stock front point) in the holder's view plane at the clamped spacing,
    // opposite each other, raised 6 units like v63, and never closer than 50 /
    // farther than 120 from the base axis.
    {
        const float base[3] = { 100, 200, 30 };
        const float holder[3] = { 0, 0, 0 };
        float a[3], b[3];
        hp3trio::lateralGoal(base, holder, 60.0f, 0, a);
        hp3trio::lateralGoal(base, holder, 60.0f, 1, b);
        float hx = base[0] - holder[0], hy = base[1] - holder[1];
        float len = std::sqrt(hx * hx + hy * hy);
        float ux = -hy / len, uy = hx / len;
        float s = 60.0f * 0.9f + 25.0f;                  // 79
        float ea[3] = { base[0] + ux * s, base[1] + uy * s, base[2] + 6 };
        float eb[3] = { base[0] - ux * s, base[1] - uy * s, base[2] + 6 };
        assert(close3(a, ea, 0.01f));
        assert(close3(b, eb, 0.01f));
    }

    puts("coop trio placement: hero/front/cluster goals, MoveSmooth chase, facing and lateral fallback passed");
    return 0;
}
