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
    assert(isLumosTriggerObject("LumosTrigger Save0.LumosTrigger0"));
    // 9b. Non-actor look-alikes carrying the token in their own name reject.
    assert(!isLumosTriggerObject("Texture hgame.LumosTriggerIcon"));   // the 1st GPF receiver
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
    // 9d. classTokenOf edge behaviour.
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

    // 10. v66.4 crash regression: the SECOND 2026-09-09 Player-2 Lumos GPF,
    //    on the same HP3_InsideHub gargoyle with v66.3 running. Crash history:
    //      UObject::ProcessEvent <- (LumosSparklesEmitter
    //         HP3_InsideHub.LumosSparklesEmitter0, Function KWGame.KWPawn.Trigger)
    //    v66.3's substring WITHIN the class token admitted that emitter class
    //    (token "LumosSparklesEmitter" contains "LumosSparkles"); the fire
    //    loop then ran pawn bytecode with the emitter as 'this'. The name rule
    //    is now EXACT equality - no substring survives anywhere near it.
    // 10a. THE v66.4 GPF receiver, verbatim from the crash history, rejects.
    assert(!isLumosTriggerObject("LumosSparklesEmitter HP3_InsideHub.LumosSparklesEmitter0"));
    assert(!isLumosTriggerClassToken("LumosSparklesEmitter"));
    // 10b. Every other within-token substring look-alike rejects too, even
    //    names that v66.3 deliberately accepted: membership by name is closed
    //    to the two proven stock classes; differently-named subclasses can
    //    only re-enter through the class-chain pointer proof (dllmain.cpp
    //    lumosChainProves), exercised via chainContainsAny below.
    assert(!isLumosTriggerObject("LumosSparkles HP3_InsideHub.LumosSparkles2"));
    assert(!isLumosTriggerObject("LumosTriggerLarge MyLevel.LumosTriggerLarge0"));
    assert(!isLumosTriggerClassToken("LumosSparkles"));
    assert(!isLumosTriggerClassToken("LumosTriggerLarge"));
    assert(!isLumosTriggerObject("LumosLight HP3_InsideHub.LumosLight0"));
    assert(!isLumosTriggerObject("LumosSparklesTriggerIcon hgame.Um"));
    assert(!isLumosTriggerClassToken(""));
    assert(!isLumosTriggerClassToken(0));
    assert(!isLumosTriggerObject("Trigger HP3_InsideHub.Trigger5"));  // Engine.Trigger, not Lumos
    assert(!isLumosTriggerObject("SpellCursor HP3_InsideHub.SpellCursor1"));
    // 10c. The exact stock names still accept, case-insensitively.
    assert(isLumosTriggerClassToken("LumosTrigger"));
    assert(isLumosTriggerClassToken("LumosSparklesTrigger"));
    assert(isLumosTriggerClassToken("lumostrigger"));
    assert(isLumosTriggerClassToken("LUMOSSPARKLESTRIGGER"));
    assert(isSecretWallClassToken("GenericColObj"));
    assert(isSecretWallClassToken("kwblockingvolume"));
    assert(!isSecretWallClassToken("GenericColObjIcon"));
    assert(!isSecretWallClassToken(0));
    // 10d. tokenEquals: strict ASCII, case-insensitive, no prefix matching.
    assert(tokenEquals("LumosTrigger", "lumosTRIGGER"));
    assert(!tokenEquals("LumosTrigger", "LumosTriggerX"));
    assert(!tokenEquals("LumosTrigger", "LumosTrigge"));
    assert(!tokenEquals("LumosTrigger", 0));
    assert(!tokenEquals(0, "LumosTrigger"));
    // 10e. chainContainsAny: the subclass admission primitive. Build fake
    //    class chains (receiver class first, as cgClassOf/cgSuper produce
    //    them) and fake family-ref tables; only true pointer equality joins.
    {
        char clsEmitter[8], clsLumosTrig[8], clsSparklesTrig[8], clsActor[8],
             clsColObj[8], clsCustomSub[8], clsObject[8];
        // Emitter chain: LumosSparklesEmitter -> ... -> Actor -> Object.
        // Contains NO family class: the v66.4 receiver must not be provable.
        void *emitterChain[] = { clsEmitter, clsActor, clsObject };
        void *trigRefs[] = { clsLumosTrig, clsSparklesTrig };
        assert(!chainContainsAny(emitterChain, 3, trigRefs, 2));
        // Stock LumosSparklesTrigger chain: itself -> LumosTrigger-ish base ->
        // Actor. Hits ref at position 0...
        void *stockChain[] = { clsSparklesTrig, clsActor, clsObject };
        assert(chainContainsAny(stockChain, 3, trigRefs, 2));
        // ...and at a deep position: custom subclass of LumosTrigger.
        void *subChain[] = { clsCustomSub, clsLumosTrig, clsActor, clsObject };
        assert(chainContainsAny(subChain, 4, trigRefs, 2));
        // Wall family proofs.
        void *wallRefs[] = { clsColObj, 0 };   // second ref unresolved in-game
        void *wallChain[] = { clsColObj, clsActor, clsObject };
        assert(chainContainsAny(wallChain, 3, wallRefs, 2));
        // Guard rails: NULL/empty inputs never match.
        assert(!chainContainsAny(0, 3, trigRefs, 2));
        assert(!chainContainsAny(stockChain, 3, 0, 2));
        assert(!chainContainsAny(stockChain, 0, trigRefs, 2));
        assert(!chainContainsAny(stockChain, 3, trigRefs, 0));
        void *noRefs[] = { 0, 0 };
        assert(!chainContainsAny(stockChain, 3, noRefs, 2));
        void *nullChain[] = { 0, 0, 0 };
        assert(!chainContainsAny(nullChain, 3, trigRefs, 2));
    }

    std::puts("lumos sync: state, timer, light, proximity, passability, "
              "v66.3 class-token and v66.4 exact-family/chain-proof scan "
              "assertions passed");
    return 0;
}
