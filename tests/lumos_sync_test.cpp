#include "../src/lumos_sync.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) <= epsilon;
}

int main()
{
    using namespace hp3lumos;

    // 1. Initial State
    State state;
    assert(!state.active);
    assert(state.sourcePlayer == -1);
    assert(state.isExpired(1000));
    assert(near(state.remainingSeconds(1000), 0.0f));

    // 2. Activation
    state.activate(1, 1000, 20000); // Player 1 (Hermione) activates Lumos for 20s
    assert(state.active);
    assert(state.sourcePlayer == 1);
    assert(!state.isExpired(1000));
    assert(!state.isExpired(15000));
    assert(near(state.remainingSeconds(1000), 20.0f));
    assert(near(state.remainingSeconds(6000), 15.0f));
    assert(state.update(10000));

    // 3. Expiration
    assert(!state.isExpired(20999));
    assert(state.isExpired(21000));
    assert(state.isExpired(25000));
    assert(!state.update(21000));
    assert(!state.active);

    // 4. Tick wrapping safety
    state.activate(2, 0xFFFFFF00u, 5000); // Activated near 32-bit tick limit
    assert(state.active);
    assert(!state.isExpired(0xFFFFFF00u));
    assert(!state.isExpired(0xFFFFFF50u));
    assert(state.isExpired((std::uint32_t)(0xFFFFFF00u + 5001)));

    // 5. Light active check
    assert(isLightActive(false, 1, 255)); // Steady, full brightness, not hidden
    assert(!isLightActive(true, 1, 255)); // Hidden
    assert(!isLightActive(false, 0, 255)); // LT_None
    assert(!isLightActive(false, 1, 0)); // 0 brightness

    // 6. Cylinder proximity check
    float center[3] = { 100.0f, 200.0f, 50.0f };
    float posInside[3] = { 150.0f, 200.0f, 60.0f };
    float posOutsideRadius[3] = { 500.0f, 200.0f, 50.0f };
    float posOutsideHeight[3] = { 100.0f, 200.0f, 300.0f };

    assert(isWithinCylinder(posInside, center, 100.0f, 50.0f));
    assert(!isWithinCylinder(posOutsideRadius, center, 100.0f, 50.0f));
    assert(!isWithinCylinder(posOutsideHeight, center, 100.0f, 50.0f));

    // 7. Multi-player proximity to secret wall trigger
    float players[3][3] = {
        { 0.0f, 0.0f, 0.0f },       // P1 (Harry) far away
        { 120.0f, 200.0f, 50.0f },   // P2 (Hermione) near trigger
        { 800.0f, 900.0f, 100.0f }   // P3 (Ron) far away
    };
    assert(anyPlayerNearTrigger(players, 3, center, 100.0f, 50.0f));

    float playersFar[3][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 500.0f, 500.0f, 50.0f },
        { 800.0f, 900.0f, 100.0f }
    };
    assert(!anyPlayerNearTrigger(playersFar, 3, center, 100.0f, 50.0f));

    // 8. Secret wall passability logic
    assert(shouldSecretWallBePassable(true, true));
    assert(!shouldSecretWallBePassable(true, false));
    assert(!shouldSecretWallBePassable(false, true));
    assert(!shouldSecretWallBePassable(false, false));

    std::puts("lumos sync: state, timer, light, proximity and passability assertions passed");
    return 0;
}
