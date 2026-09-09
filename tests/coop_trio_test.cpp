// Host-side tests for the cooperative "three floating spells" placement
// policy: the exact stock per-hero gesture goal (each hero's own vLOS_End),
// MoveSmooth chase semantics, and facing yaw. No engine ABI.
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

    // The exact original 1-to-3 look: two companions on opposite sides of the
    // target get two DIFFERENT goals, one on each hero's own line of sight -
    // the fix for v64, where a shared front point plus a fixed lateral
    // spacing collapsed the two icons onto each other.
    {
        const float a[3] = { 400, 200, 30 };   // +X of the target
        const float b[3] = { 100, 0, 30 };     // -Y of the target
        float ga[3], gb[3];
        hp3trio::heroGoal(centre, a, dist, ga);
        hp3trio::heroGoal(centre, b, dist, gb);
        const float ea[3] = { 168, 200, 30 };
        const float eb[3] = { 100, 132, 30 };
        assert(!close3(ga, gb, 0.5f));                       // distinct
        assert(close3(ga, ea, 0.001f));
        assert(close3(gb, eb, 0.001f));
    }
    // Companions on the SAME side still land apart when their lines differ
    // (fanning out), instead of collapsing to one point.
    {
        const float a[3] = { 400, 260, 30 };
        const float b[3] = { 400, 140, 30 };
        float ga[3], gb[3];
        hp3trio::heroGoal(centre, a, dist, ga);
        hp3trio::heroGoal(centre, b, dist, gb);
        assert(!close3(ga, gb, 0.5f));
        assert(std::fabs(ga[0] - 100.0f - dist * 300.0f / std::sqrt(300.0f*300.0f + 60.0f*60.0f)) < 0.001f);
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
    // samples move rate*dt of the remaining distance and converge.
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
        // The flicker rate (8) is slower than the locked rate (10): the same
        // step moves 12.8% instead of 16%.
        float fpos[3]; std::memcpy(fpos, from, sizeof(fpos));
        bool fstarted = false;
        hp3trio::moveSmooth(fpos, fstarted, g2, 8.0f, 0.016f);
        assert(fstarted && close3(fpos, g2, 0.001f));   // first sample snaps
        hp3trio::moveSmooth(fpos, fstarted, g2, 8.0f, 0.016f);
        assert(close3(fpos, g2, 0.001f));               // already there
        float g3[3] = { 300, 0, 0 };
        hp3trio::moveSmooth(fpos, fstarted, g3, 8.0f, 0.016f);
        assert(std::fabs(fpos[0] - (200.0f + 100.0f * 0.128f)) < 0.01f);
        // A zero/negative dt is a no-op (stock would pass the frame delta).
        float before = pos[0];
        hp3trio::moveSmooth(pos, started, g2, 10.0f, 0.0f);
        assert(pos[0] == before);
        // Converge after enough frames.
        for (int k = 0; k < 200; k++)
            hp3trio::moveSmooth(pos, started, g2, 10.0f, 0.05f);
        assert(close3(pos, g2, 0.01f));
    }

    puts("coop trio placement: stock per-hero goals, MoveSmooth chase and facing passed");
    return 0;
}
