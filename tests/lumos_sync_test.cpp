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

    // 11. v66.5 regressions: octree-safe wall lifecycle + follow-light timer.
    //     Field report (v66.4, HP3_InsideHub gargoyle, Player 2 casts Lumos):
    //     the revealed secret wall stayed solid ("doesn't seem to apply
    //     correct state") and quitting the game crashed with
    //     "Assertion failed: Actor->bCollideActors [File:UnOctree.cpp
    //     [Line: 1598]]" - FCollisionOctree::RemoveActor <- ... <- ULevel::
    //     Destroy. Both had one root cause: the wall replication wrote the
    //     collision BITS directly into wall actors while the engine resolves
    //     collision through the collision octree, which only the
    //     Engine.Actor.SetCollision native updates.
    // 11a. wallShouldBeOpen truth table: the v66.5 open policy adds the
    //     secretCandidate gate to v66.1's shouldSecretWallBePassable - a
    //     wall is only ever opened while Lumos is active, the wall is a
    //     PROVEN secret wall (paired to a lumos trigger at scan time), and a
    //     player is actually at it. Ordinary GenericColObj/KWBlockingVolume
    //     proxies within 300 units of a player must stay solid.
    assert(wallShouldBeOpen(true, true, true));    // the only open case
    assert(!wallShouldBeOpen(true, true, false));  // nobody at the wall
    assert(!wallShouldBeOpen(true, false, true));  // ordinary wall (v66.5 gate)
    assert(!wallShouldBeOpen(true, false, false));
    assert(!wallShouldBeOpen(false, true, true));  // Lumos expired
    assert(!wallShouldBeOpen(false, true, false));
    assert(!wallShouldBeOpen(false, false, true));
    assert(!wallShouldBeOpen(false, false, false));
    // 11b. pack/unpack roundtrip: every one of the 8 bit combinations a
    //     level can load a wall with survives the snapshot -> restore trip
    //     EXACTLY (a close must hand the SetCollision native the very bits
    //     the level started with, e.g. bCollideActors+bBlockPlayers only).
    for (unsigned bits = 0; bits < 8; ++bits) {
        WallCollisionBits in;
        in.collideActors = (bits & 1) != 0;
        in.blockActors   = (bits & 2) != 0;
        in.blockPlayers  = (bits & 4) != 0;
        WallCollisionBits out = unpackWallBits(packWallBits(
            in.collideActors, in.blockActors, in.blockPlayers));
        assert(out.collideActors == in.collideActors);
        assert(out.blockActors   == in.blockActors);
        assert(out.blockPlayers  == in.blockPlayers);
    }
    // The hub's stock secret-wall shape: collide+block actors+block players.
    assert(packWallBits(true, true, true) == 7);
    assert(packWallBits(false, false, false) == 0);
    // WallBitsUnknown never decodes as a real combination: unpacking it is
    // never a valid restore, and the wall loop treats it as "untouchable".
    assert(WallBitsUnknown == 0xFF);
    {
        WallCollisionBits bad = unpackWallBits(WallBitsUnknown);
        (void)bad;   // 0xFF decodes to all-true, but the loop guards on the
                     // SENTINEL before ever unpacking - assert the sentinel
                     // differs from every legal pack result instead:
        for (unsigned bits = 0; bits < 8; ++bits) {
            WallCollisionBits b;
            b.collideActors = (bits & 1) != 0;
            b.blockActors   = (bits & 2) != 0;
            b.blockPlayers  = (bits & 4) != 0;
            assert(packWallBits(b.collideActors, b.blockActors,
                                b.blockPlayers) != WallBitsUnknown);
        }
    }
    // 11c. State::refresh is an extend-only ratchet: while a wand light
    //     genuinely burns (isLightActive), the state follows it in
    //     FollowLightRefreshMs=1000ms steps; refresh can NEVER shorten a
    //     still-running window, and the state dies within a second of the
    //     last light instead of DefaultLumosDurationMs after it.
    assert(FollowLightRefreshMs == 1000);
    {
        State follow;
        follow.activate(1, 1000, 10000);            // expires at 11000
        follow.refresh(11000 - 1000, FollowLightRefreshMs);  // extend to 11000
        assert(!follow.isExpired(10999));
        assert(follow.isExpired(11000));
        // A SHORTER proposed window never pulls the expiry back in.
        follow.activate(1, 5000, 5000);             // expires at 10000...
        follow.refresh(5001, 1);                    // ...proposes 5002 < 10000
        assert(!follow.isExpired(9999));            // still the longer window
        assert(follow.isExpired(10001));
        // Chained 1s ratchets track a burning light indefinitely...
        State burning;
        burning.activate(0, 0, FollowLightRefreshMs);
        std::uint32_t t = 0;
        for (int i = 0; i < 120; ++i) {             // two minutes of light
            t += 1000;
            burning.refresh(t, FollowLightRefreshMs);
            assert(burning.update(t));
        }
        assert(burning.active);                     // never expired mid-burn
        // ...and once the light goes out the last ratchet is all that is
        // left: expiry within FollowLightRefreshMs of the final refresh.
        std::uint32_t lastT = t;
        assert(burning.isExpired(lastT + FollowLightRefreshMs));
        assert(!burning.isExpired(lastT + FollowLightRefreshMs - 1));
        // Tick-wrap ratchet: refresh across the 32-bit boundary extends.
        // start 0xFFFFFF00 + 1000ms -> expiry wraps to 0x2E8 (744).
        State wrap;
        wrap.activate(2, 0xFFFFFF00u, 1000);
        // refresh at now = 0xFFFFFF00+500 (wraps to 0xF4=244) proposes
        // 244+1000 = 0x4DC (1244) - strictly later than 744, so it extends.
        wrap.refresh(0xFFFFFF00u + 500u, 1000);
        assert(!wrap.isExpired(0xFFFFFF00u + 600u));   // 344: still burning
        assert(!wrap.isExpired(0x4DBu));
        assert(wrap.isExpired(0x4DCu));                // new expiry
        // A refresh whose proposal lands BEFORE the current expiry never
        // shortens the window (extend-only ratchet).
        wrap.refresh(0x10u, 1000);                     // proposes 0x410 < 0x4DC
        assert(wrap.isExpired(0x4DCu));                // expiry unchanged
        assert(!wrap.isExpired(0x40Fu));
    }

    // -----------------------------------------------------------------------
    // 12. v67: the stock Lumos chain, replicated. Decompiled HP2 lineage
    //     (HP3 inherits it verbatim): LumosLight.TurnOn / TurnDynamicLightOn,
    //     the 30 s fLumosTimeToTurnOff window, and LumosTrigger's once-per-
    //     level Event fire keyed on a proximity check that stock only ever
    //     runs for PlayerHarry.
    // -----------------------------------------------------------------------
    // 12a. The stock light register, on and off. Script declares brightness
    //      400; a byte property truncates to 255, a float keeps 400.0 - the
    //      register carries both and the caller picks by layout.
    {
        LumosLightRegister on = stockLumosLightOn();
        assert(on.lightType   == 1);    // LT_Steady
        assert(on.lightEffect == 13);   // LE_NonIncidence
        assert(on.brightnessByte == 255);
        assert(near(on.brightnessFloat, 400.0f));
        assert(on.hue == 32);
        assert(on.saturation == 72);
        assert(near(on.radius, 15.0f));
        assert(near(on.radiusInner, 5.0f));

        LumosLightRegister off = stockLumosLightOff();
        assert(off.lightType == 0 && off.lightEffect == 0);
        assert(off.brightnessByte == 0 && off.brightnessFloat == 0.0f);
        assert(off.hue == 0 && off.saturation == 0);
        assert(near(off.radius, 0.0f) && near(off.radiusInner, 0.0f));
    }
    // 12b. Layout tells. LightHue (a byte) is declared right after
    //      LightBrightness: delta 1 => byte property, delta >= 4 => float.
    //      A wrong guess either never sees a burning light (float 400.0f's
    //      low byte is 0x00) or corrupts the hue byte next to it.
    assert(!brightnessIsFloatByLayout(0x1A2, 0x1A3));   // adjacent bytes
    assert(!brightnessIsFloatByLayout(0x1A2, 0x1A4));   // 2 apart: still byte+pad
    assert( brightnessIsFloatByLayout(0x1A2, 0x1A6));   // float + 4-byte step
    assert(!brightnessIsFloatByLayout(0, 0x1A3));       // unresolvable -> byte
    assert(!brightnessIsFloatByLayout(-1, 0x1A3));
    // LightRadiusInner beside float LightRadius: adjacent (4) => float.
    assert(radiusInnerIsFloatByLayout(0x1B0, 0x1B4));
    assert(!radiusInnerIsFloatByLayout(0x1B0, 0x1B5));
    assert(!radiusInnerIsFloatByLayout(0x1B0, -1));
    assert(!radiusInnerIsFloatByLayout(-1, 0x1B4));
    // Type-aware "light is burning": the byte variant of a burning float
    // light reads 0 - the exact v66.5 detection blindness this fixes.
    assert(isLightActiveF(false, 1, 400.0f));
    assert(isLightActiveF(false, 1, 0.5f));
    assert(!isLightActiveF(false, 0, 400.0f));
    assert(!isLightActiveF(false, 1, 0.0f));
    assert(!isLightActiveF(true, 1, 400.0f));
    // 12c. Stock LumosTrigger.InLumosRadius: VSize(trigger - pawn) < radius,
    //      with the optional |dZ| gate. Stock defaults 512 / 64.
    {
        float trig[3] = { 1000.0f, 2000.0f, 100.0f };
        assert(StockTriggerDistanceCheck  == 512.0f);
        assert(StockTriggerZDistanceCheck == 64.0f);
        float at512[3]  = { 1200.0f, 2200.0f, 160.0f };  // dist ~569? no: 200,200,60 -> 291
        float at513[3]  = { 1000.0f + 513.0f, 2000.0f, 100.0f };
        float at511[3]  = { 1000.0f + 511.0f, 2000.0f, 100.0f };
        assert(stockTriggerInRadius(trig, at512, 512.0f, false, 64.0f));
        assert(!stockTriggerInRadius(trig, at513, 512.0f, false, 64.0f));
        assert(stockTriggerInRadius(trig, at511, 512.0f, false, 64.0f));
        // The Z gate: 3D radius ok, but |dZ| >= fZDistanceCheck rejects.
        float high[3] = { 1000.0f, 2000.0f, 100.0f + 100.0f };
        assert(stockTriggerInRadius(trig, high, 512.0f, false, 64.0f));
        assert(!stockTriggerInRadius(trig, high, 512.0f, true, 64.0f));
        float highOk[3] = { 1000.0f, 2000.0f, 100.0f + 63.0f };
        assert(stockTriggerInRadius(trig, highOk, 512.0f, true, 64.0f));
        // A companion standing where stock never checks - the whole point:
        // the SAME predicate, evaluated for ANY player, is what opens walls.
        float companion[3] = { 1000.0f + 100.0f, 2000.0f, 100.0f };
        assert(stockTriggerInRadius(trig, companion, 512.0f, true, 64.0f));
    }
    // 12d. triggerShouldFire: the stock state machine minus the
    //      PlayerHarry-only key. Lumos burning, event linked, nobody has
    //      fired it yet (stock bFirstEventSent or the mod latch), a player
    //      actually inside the trigger's own radius.
    assert(triggerShouldFire(true, true, false, true));    // the fire case
    assert(!triggerShouldFire(false, true, false, true));  // Lumos expired
    assert(!triggerShouldFire(true, false, false, true));  // no Event linked
    assert(!triggerShouldFire(true, true, true, true));    // already fired
    assert(!triggerShouldFire(true, true, false, false));  // nobody in radius
    // 12e. companionLightShouldTurnOn: a cold light turns on while the state
    //      is on; a stock-burning light (the lead's) and an already-latched
    //      light are left alone.
    assert(companionLightShouldTurnOn(true, false, false));
    assert(!companionLightShouldTurnOn(true, true, false));   // stock burning
    assert(!companionLightShouldTurnOn(true, false, true));   // mod latched
    assert(!companionLightShouldTurnOn(false, false, false)); // Lumos expired
    // 12f. The stock window is 30 s (fLumosTimeToTurnOff = 30.0), and the
    //      default duration is that stock value - a replicated companion
    //      light must not outlive (or undershoot) the lead's window.
    assert(DefaultLumosDurationMs == 30000);
    assert(StockLumosDurationMs == DefaultLumosDurationMs);
    {
        State s;
        s.activate(1, 0, DefaultLumosDurationMs);
        assert(!s.isExpired(29999));
        assert(s.isExpired(30000));
    }

    std::puts("lumos sync: state, timer, light, proximity, passability, "
              "v66.3 class-token, v66.4 exact-family/chain-proof scan, "
              "v66.5 wall-bits/follow-light and v67 stock-register/"
              "trigger-fire assertions passed");
    return 0;
}
