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

    // 9. v66.3 crash regression: the per-level scan must accept only objects
    //    whose CLASS token is the Lumos trigger / secret wall family. Replays
    //    the exact 2026-09-09 Player-2 Lumos GPF, whose history named
    //    (Texture hgame.LumosTriggerIcon, Function KWGame.KWPawn.Trigger):
    //    the old whole-string substring cache admitted that HUD texture and
    //    later fired KWPawn.Trigger on it.
    // 9a. Placed level actors accept (class token == wanted family).
    assert(isLumosTriggerObject("LumosTrigger HP3_InsideHub.LumosTrigger3"));
    assert(isLumosTriggerObject("LumosSparklesTrigger HP3_InsideHub.LumosSparklesTrigger7"));
    assert(isLumosTriggerObject("LumosSparkles HP3_InsideHub.LumosSparkles2"));
    assert(isLumosTriggerObject("LumosTrigger Save0.LumosTrigger0"));
    // 9b. Non-actor look-alikes carrying the token in their own name reject.
    assert(!isLumosTriggerObject("Texture hgame.LumosTriggerIcon"));   // the GPF receiver
    assert(!isLumosTriggerObject("Class hgame.LumosTrigger"));
    assert(!isLumosTriggerObject("Class hgame.LumosSparklesTrigger"));
    assert(!isLumosTriggerObject("Function hgame.LumosTrigger.PostTouch"));
    assert(!isLumosTriggerObject("Sound hgame.Sounds.LumosTriggerOn"));
    assert(!isLumosTriggerObject("LumosSpell HP3_InsideHub.LumosSpell1"));
    assert(!isLumosTriggerObject("WetTexture hgame.SpellFX.LumosWet1"));
    assert(!isLumosTriggerObject(""));
    assert(!isLumosTriggerObject(0));
    // 9c. Secret walls: placed actors accept, class/asset look-alikes reject.
    assert(isSecretWallObject("GenericColObj HP3_InsideHub.GenericColObj12"));
    assert(isSecretWallObject("KWBlockingVolume HP3_InsideHub.KWBlockingVolume5"));
    assert(!isSecretWallObject("Class KWGame.KWBlockingVolume"));
    assert(!isSecretWallObject("Class KWGame.GenericColObj"));
    assert(!isSecretWallObject("Texture KWGame.Tex.GenericColObjSkin"));
    assert(!isSecretWallObject("Mover HP3_InsideHub.Mover16"));
    assert(!isSecretWallObject(0));
    // 9d. Substring still works within the class token itself (subclasses).
    assert(isLumosTriggerObject("LumosTriggerLarge MyLevel.LumosTriggerLarge0"));
    // 9e. classTokenOf edge behaviour.
    {
        char tok[64];
        classTokenOf("LumosTrigger HP3_InsideHub.LumosTrigger3", tok, sizeof(tok));
        assert(0 == std::strcmp(tok, "LumosTrigger"));
        classTokenOf("NoSpaceHere", tok, sizeof(tok));
        assert(0 == std::strcmp(tok, "NoSpaceHere"));
        classTokenOf(0, tok, sizeof(tok));
        assert(0 == std::strcmp(tok, ""));
        char tiny[6];
        classTokenOf("LumosSparklesTrigger X.Y", tiny, sizeof(tiny));
        assert(0 == std::strcmp(tiny, "Lumos"));   // truncated, no overflow, still terminated
    }

    std::puts("lumos sync: state, timer, light, proximity, passability and "
              "v66.3 class-token scan assertions passed");
    return 0;
}
