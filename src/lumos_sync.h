#ifndef HP3_LUMOS_SYNC_H
#define HP3_LUMOS_SYNC_H

#include <cstdint>
#include <cmath>
#include <cstring>

namespace hp3lumos {

// Default duration for Lumos in milliseconds if not specified by game.
// v67: aligned with the STOCK value - decompiled LumosLight.uc (HP2 lineage,
// which HP3 inherits verbatim - the HP3 save chain harry.Wand ->
// HarryWand.TheLumosLight -> LumosLight proves the same layout) auto-TurnOffs
// after fLumosTimeToTurnOff = 30.0 seconds.
static const std::uint32_t DefaultLumosDurationMs = 30000;
static const std::uint32_t StockLumosDurationMs   = DefaultLumosDurationMs;
// v66.5: while a stock wand light is genuinely burning, the mod's state
// follows it in short ratchets - refresh() only ever EXTENDS the expiry, so
// an infinite-Lumos gargoyle keeps secret walls openable for exactly as
// long as its light burns, and the state dies within a second of the last
// light instead of DefaultLumosDurationMs after it.
static const std::uint32_t FollowLightRefreshMs = 1000;
// Proximity radius (units) to detect and activate Lumos triggers/sparkles around secret walls
static const float DefaultTriggerRadius = 350.0f;
static const float DefaultTriggerHeight = 150.0f;
// v66.5: a cached wall only counts as a SECRET wall if a cached
// LumosTrigger / LumosSparklesTrigger sits within this radius of it - the
// stock game places one of those at every wall Lumos reveals (its own
// InLumosRadius check measures PlayerHarry's distance from THAT actor, HP2
// default 512, and a large wall's centre can sit a few hundred units past
// its surface). Every other GenericColObj / KWBlockingVolume in the level
// is an ordinary collision proxy (railings, camera blockers, invisible
// bounds - 80 of them cached in HP3_InsideHub) and must never be opened.
static const float SecretWallPairRadius = 900.0f;

struct LightProperties {
    unsigned char lightType;        // 0 = LT_None, 1 = LT_Steady
    unsigned char lightEffect;
    unsigned char lightBrightness;  // 0 .. 255
    unsigned char lightHue;
    unsigned char lightSaturation;
    float lightRadius;
    bool bHidden;
    bool bCollideActors;
};

inline bool isLightActive(bool bHidden, unsigned char lightType, unsigned char lightBrightness)
{
    return !bHidden && (lightType != 0) && (lightBrightness > 0);
}

// ---------------------------------------------------------------------------
// v67: WHAT STOCK LUMOS ACTUALLY DOES TO THE MAIN CHARACTER
// (decompiled HP2 lineage LumosLight.uc / LumosTrigger.uc / baseWand.uc /
// gargoyle.uc - HP3 inherits the same classes; the HP3 save-game chain
// "harry.Wand -> HarryWand.TheLumosLight -> LumosLight" is verbatim):
//
//   gargoyle.HandleSpellLumos -> baseWand(PlayerHarry.Weapon).LumosTurnOn()
//   -> TheLumosLight.TurnOn():
//        fLumosTime = 0;
//        if (PlayerHarry.bLumosOn) return;        <-- single-player global gate
//        bLumosOn = True; PlayerHarry.bLumosOn = True; Enable('Tick');
//        TurnDynamicLightOn();                    -> the register below
//        foreach AllActors(Actor, A) A.OnLumosOn();   arms every LumosTrigger
//        Particles = Spawn(Class'LumosLightFX', self, , Location);  <- THE
//                                                    visible wand glow
//   The wand's own Tick then calls TheLumosLight.UpdateLocation(WandEndPoint)
//   every frame while TheLumosLight.bLumosOn (light + particles ride the wand
//   tip), and LumosLight.Tick auto-TurnOff()s after fLumosTimeToTurnOff = 30s
//   (bInfiniteLumos gargoyles never do). TurnOff(): PlayerHarry.bLumosOn =
//   False, TurnDynamicLightOff(), broadcast OnLumosOff(), Particles.Destroy().
//
//   LumosTrigger (one placed at every secret wall) arms on OnLumosOn and fires
//   TriggerEvent(Event, self, None) EXACTLY ONCE (bFirstEventSent is never
//   reset) as soon as PlayerHarry comes within fDistanceCheck (512 default) -
//   the Event opens the linked wall (a Mover: TriggerToggle, MoveTime=0).
//   Walls open permanently; nothing fires when the hero leaves the radius
//   (bEventLeaving defaults False).
//
//   THE SPLITSCREEN GAP: that chain hardcodes PlayerHarry everywhere - it
//   lights the LEAD's wand when anyone's spell hits the gargoyle, and it only
//   ever measures the LEAD's distance to the wall trigger. A companion never
//   gets a lit wand and never opens a wall, no matter what the mod pokes.
// ---------------------------------------------------------------------------

// LumosLight.TurnDynamicLightOn/Off - the exact register the stock script
// writes. LightBrightness is declared 400 in script; on a byte property that
// truncates to 255, on a float property it stays 400.0 (the shipped engine is
// a UE1.5/UE2 hybrid, so the mod detects which representation is live from the
// class layout - see brightnessIsFloatByLayout).
static const unsigned char StockLightTypeOn           = 1;     // LT_Steady
static const unsigned char StockLightEffectOn         = 13;    // LE_NonIncidence
static const unsigned char StockLightBrightnessOnByte = 255;
static const float StockLightBrightnessOnFloat        = 400.0f;
static const unsigned char StockLightHueOn            = 32;
static const unsigned char StockLightSaturationOn     = 72;
static const float StockLightRadiusOn                 = 15.0f;
static const float StockLightRadiusInnerOn            = 5.0f;

struct LumosLightRegister {
    unsigned char lightType;
    unsigned char lightEffect;
    unsigned char brightnessByte;   // meaningful when !brightnessIsFloat
    float         brightnessFloat;  // meaningful when  brightnessIsFloat
    unsigned char hue;
    unsigned char saturation;
    float         radius;
    float         radiusInner;
};

inline LumosLightRegister stockLumosLightOn(void)
{
    LumosLightRegister r;
    r.lightType      = StockLightTypeOn;
    r.lightEffect    = StockLightEffectOn;
    r.brightnessByte = StockLightBrightnessOnByte;
    r.brightnessFloat = StockLightBrightnessOnFloat;
    r.hue            = StockLightHueOn;
    r.saturation     = StockLightSaturationOn;
    r.radius         = StockLightRadiusOn;
    r.radiusInner    = StockLightRadiusInnerOn;
    return r;
}

inline LumosLightRegister stockLumosLightOff(void)
{
    LumosLightRegister r;
    r.lightType      = 0;  // LT_None
    r.lightEffect    = 0;  // LE_None
    r.brightnessByte = 0;
    r.brightnessFloat = 0.0f;
    r.hue            = 0;
    r.saturation     = 0;
    r.radius         = 0.0f;
    r.radiusInner    = 0.0f;
    return r;
}

// LightHue (a byte) is declared directly after LightBrightness: layout delta
// 1 => both bytes (UE1.5), delta >= 4 => brightness is a float (UE2). This is
// the only honest tell the mod can read from outside the engine - a wrong
// guess would either never see the light burn (float read as byte 0x00 of
// 400.0f) or corrupt the neighbouring hue byte (byte written over a float).
inline bool brightnessIsFloatByLayout(int offBrightness, int offHue)
{
    return offBrightness > 0 && offHue > 0 && (offHue - offBrightness) >= 4;
}

// LightRadiusInner sits directly beside float LightRadius: adjacent (4) means
// the same float representation; anything else - do not write it.
inline bool radiusInnerIsFloatByLayout(int offRadius, int offRadiusInner)
{
    if (offRadius <= 0 || offRadiusInner <= 0) return false;
    int d = offRadiusInner - offRadius;
    if (d < 0) d = -d;
    return d == 4;
}

// Type-aware "is a light burning": float brightness 400.0 read as a byte would
// be 0x00 and look OFF - the v66.5 detection could never see a stock-burning
// light on a float-brightness engine.
inline bool isLightActiveF(bool bHidden, unsigned char lightType, float brightness)
{
    return !bHidden && (lightType != 0) && (brightness > 0.0f);
}

// Stock LumosTrigger.InLumosRadius: VSize(Location - pawnLoc) < fDistanceCheck,
// with the optional |dZ| < fZDistanceCheck gate (bUseZDistanceCheck).
inline bool stockTriggerInRadius(const float triggerLoc[3], const float pawnLoc[3],
                                 float fDistanceCheck, bool useZCheck, float fZDistanceCheck)
{
    float dx = pawnLoc[0] - triggerLoc[0];
    float dy = pawnLoc[1] - triggerLoc[1];
    float dz = pawnLoc[2] - triggerLoc[2];
    if (useZCheck) {
        if (dz < 0) dz = -dz;
        if (dz >= fZDistanceCheck) return false;
    }
    return (dx * dx + dy * dy + dz * dz) < fDistanceCheck * fDistanceCheck;
}

// Stock defaultproperties of LumosTrigger.
static const float StockTriggerDistanceCheck  = 512.0f;
static const float StockTriggerZDistanceCheck = 64.0f;

// The trigger-event fire policy: the stock state machine minus the
// PlayerHarry-only proximity key. lumosActive = the LumosLight chain is on
// (the stock OnLumosOn broadcast already armed the trigger), eventLinked = the
// trigger has a non-empty Event, alreadyFired = stock's bFirstEventSent or the
// mod's own once-per-level latch, playerNear = ANY tracked player inside the
// trigger's own radius (stock only ever checks the lead; that check is exactly
// what companions must replicate).
inline bool triggerShouldFire(bool lumosActive, bool eventLinked,
                              bool alreadyFired, bool playerNear)
{
    return lumosActive && eventLinked && !alreadyFired && playerNear;
}

// Per-companion wand-light sync decision: while the mod's Lumos state is on,
// a companion light that is not burning gets the stock TurnOn register.
inline bool companionLightShouldTurnOn(bool lumosActive, bool lightBurning,
                                       bool modAlreadyTurnedOn)
{
    return lumosActive && !lightBurning && !modAlreadyTurnedOn;
}

struct State {
    bool active;
    int sourcePlayer;
    std::uint32_t startTick;
    std::uint32_t expireTick;
    LightProperties sourceLight;

    State()
        : active(false)
        , sourcePlayer(-1)
        , startTick(0)
        , expireTick(0)
    {
        std::memset(&sourceLight, 0, sizeof(sourceLight));
    }

    void activate(int player, std::uint32_t nowMs, std::uint32_t durationMs = DefaultLumosDurationMs)
    {
        active = true;
        sourcePlayer = player;
        startTick = nowMs;
        expireTick = nowMs + durationMs;
    }

    void refresh(std::uint32_t nowMs, std::uint32_t durationMs = DefaultLumosDurationMs)
    {
        if (active) {
            std::uint32_t newExpire = nowMs + durationMs;
            // Wrap-safe "strictly later" test, the same comparison isExpired
            // uses: a plain > across the 32-bit tick wrap would treat a
            // refresh from just before the wrap as later than one just after
            // it and move the expiry BACKWARDS.
            if (static_cast<std::int32_t>(newExpire - expireTick) > 0) {
                expireTick = newExpire;
            }
        } else {
            activate(sourcePlayer >= 0 ? sourcePlayer : 0, nowMs, durationMs);
        }
    }

    void deactivate()
    {
        active = false;
        sourcePlayer = -1;
        startTick = 0;
        expireTick = 0;
        std::memset(&sourceLight, 0, sizeof(sourceLight));
    }

    bool isExpired(std::uint32_t nowMs) const
    {
        if (!active) return true;
        // Handle 32-bit tick wrapping safely
        return static_cast<std::int32_t>(nowMs - expireTick) >= 0;
    }

    float remainingSeconds(std::uint32_t nowMs) const
    {
        if (!active || isExpired(nowMs)) return 0.0f;
        return static_cast<float>(expireTick - nowMs) / 1000.0f;
    }

    bool update(std::uint32_t nowMs)
    {
        if (active && isExpired(nowMs)) {
            deactivate();
            return false;
        }
        return active;
    }
};

inline bool isWithinCylinder(const float pos[3], const float center[3], float radius, float height)
{
    float dx = pos[0] - center[0];
    float dy = pos[1] - center[1];
    float dz = pos[2] - center[2];
    if (std::fabs(dz) > height) return false;
    return (dx * dx + dy * dy) <= (radius * radius);
}

inline bool anyPlayerNearTrigger(const float playerPositions[][3], int numPlayers,
                                 const float triggerLoc[3], float radius, float height)
{
    for (int i = 0; i < numPlayers; ++i) {
        if (isWithinCylinder(playerPositions[i], triggerLoc, radius, height)) {
            return true;
        }
    }
    return false;
}

// v66.1 policy, retained for the regression suite. SUPERSEDED at v66.5 by
// wallShouldBeOpen below, which adds the secretCandidate gate: proximity to
// a player alone is no longer enough, the wall must also be paired to a
// lumos trigger, or it is an ordinary collision proxy.
inline bool shouldSecretWallBePassable(bool lumosActive, bool playerNear)
{
    return lumosActive && playerNear;
}

// ---------------------------------------------------------------------------
// v66.5 wall lifecycle policy. The v66..v66.4 replication wrote the
// bCollideActors/bBlockActors/bBlockPlayers BITS directly into cached wall
// actors. The engine resolves actor collision through the collision OCTREE,
// and only the native Engine.Actor.SetCollision ever adds or removes an
// actor from it - so a poked wall kept blocking (the field report: "spell
// works, but I still can't go through wall"), and when the level was
// destroyed the octree destructor walked its still-present members and hit
// the engine's own consistency check on a member whose flag the mod had
// cleared:
//   Assertion failed: Actor->bCollideActors [File:UnOctree.cpp] [Line 1598]
//   FCollisionOctree::RemoveActor <- FOctreeNode::RemoveAllActors <- ...
//   <- FCollisionOctree::~FCollisionOctree <- ULevel::Destroy
//   <- ... <- UObject::StaticExit <- appPreExit        (quit-the-game crash)
// Every wall transition therefore goes through the engine native now, and
// the policy below only decides WHEN: a wall may open while Lumos is
// active, the wall is a proven secret wall (paired to a lumos trigger at
// scan time) and a player is actually at it.
// ---------------------------------------------------------------------------

// Packed pristine collision bits for one wall, snapshotted once per level
// at scan time so a close restores exactly what the level loaded with.
// WallBitsUnknown marks a wall whose bits could not be read (or a
// non-candidate wall): it is never opened and never written.
static const unsigned char WallBitsUnknown = 0xFF;

struct WallCollisionBits {
    bool collideActors;
    bool blockActors;
    bool blockPlayers;
};

inline unsigned char packWallBits(bool collideActors, bool blockActors,
                                  bool blockPlayers)
{
    return (unsigned char)((collideActors  ? 1 : 0) |
                           (blockActors    ? 2 : 0) |
                           (blockPlayers   ? 4 : 0));
}

inline WallCollisionBits unpackWallBits(unsigned char packed)
{
    WallCollisionBits b;
    b.collideActors = (packed & 1) != 0;
    b.blockActors   = (packed & 2) != 0;
    b.blockPlayers  = (packed & 4) != 0;
    return b;
}

// v66.5: the only open condition. secretCandidate=false keeps every
// ordinary GenericColObj/KWBlockingVolume solid - the v66.x rule opened ANY
// cached wall within 300 units of ANY player while Lumos was on.
inline bool wallShouldBeOpen(bool lumosActive, bool secretCandidate,
                             bool playerNear)
{
    return lumosActive && secretCandidate && playerNear;
}

// ---------------------------------------------------------------------------
// v66.3 crash fix: the per-level Lumos trigger/secret-wall scan classified
// objects by substring-matching the WHOLE GetFullName string
// ("ClassName Package.Object"). That also collects non-actor objects that
// merely carry the token in their own name: the UClass itself
// ("Class hgame.LumosTrigger") and - fatally - the spell's HUD texture
// ("Texture hgame.LumosTriggerIcon"), which the engine loads alongside Lumos.
// A cached Texture passes every fire-time guard in lumosTick (it IS a live
// UObject, and the Location/CollisionRadius reads at actor offsets land in
// the shared UObject allocator arena, so they return garbage that can pass
// the proximity test) and is then handed to ProcessEvent as the receiver of
// KWGame.KWPawn.Trigger - pawn bytecode running on a texture-sized object.
// That was the first 2026-09-09 general protection fault:
//   UObject::ProcessEvent <- (Texture hgame.LumosTriggerIcon,
//      Function KWGame.KWPawn.Trigger) <- FPlayerSceneNode::Render
//
// v66.4 crash fix (the SECOND 2026-09-09 Player-2 Lumos GPF, on the SAME
// gargoyle, with v66.3 running):
//   UObject::ProcessEvent <- (LumosSparklesEmitter
//      HP3_InsideHub.LumosSparklesEmitter0, Function KWGame.KWPawn.Trigger)
//      <- FPlayerSceneNode::Render
// The v66.3 class-token gate substring-matched WITHIN the class token
// (strstr(clsTok, "LumosSparkles")), so the placed secret-wall sparkles
// actor - class token "LumosSparklesEmitter" - still joined the trigger
// cache, and the fire-time chain re-verify reused the same substring test
// behind an isActor gate that an Emitter trivially passes (the hub scan
// line showed "21 triggers" cached for that level). The fire loop then ran
// KWGame.KWPawn.Trigger with an emitter as 'this' - pawn bytecode touching
// pawn member offsets far past an emitter's allocation. Class-token
// membership is therefore EXACT now: the only trigger tokens are the two
// classes the stock game actually places at secret walls ("LumosTrigger",
// "LumosSparklesTrigger"), the only wall tokens are "GenericColObj" and
// "KWBlockingVolume". A subclass of one of those can no longer slip in
// through its name at all - subclasses are admitted exclusively through the
// calibrated class-CHAIN pointer proof in dllmain.cpp (lumosChainProves,
// built on chainContainsAny below), never through a string.
// ---------------------------------------------------------------------------

// Copy the class token (the word before the first space) out of a GetFullName
// string. A string with no space yields the whole string, which simply never
// matches the wanted class families below.
inline void classTokenOf(const char* fullName, char* out, unsigned cap)
{
    unsigned k = 0;
    if (!out || !cap) return;
    out[0] = 0;
    if (!fullName) return;
    while (fullName[k] && fullName[k] != ' ' && k + 1 < cap) {
        out[k] = fullName[k];
        k++;
    }
    out[k] = 0;
}

// ASCII case-insensitive equality (class tokens come out of GetFullName with
// the package's own capitalisation; compare defensively anyway).
inline bool tokenEquals(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == 0 && *b == 0;
}

// v66.4: EXACT class-token equality against the two classes the stock game
// places at secret walls. NO substring anywhere: "LumosSparklesEmitter" (the
// second 2026-09-09 GPF receiver), "LumosTriggerLarge", "LumosLight",
// "LumosSpell" and every other look-alike reject here. A true subclass of
// LumosTrigger/LumosSparklesTrigger re-enters ONLY via the dllmain.cpp class
// chain proof (lumosChainProves), which pointer-compares the receiver's
// UObject::Class -> UStruct::SuperField chain against the resolved family
// class objects - the one comparison a name can never fake.
inline bool isLumosTriggerClassToken(const char* clsTok)
{
    return tokenEquals(clsTok, "LumosTrigger") ||
           tokenEquals(clsTok, "LumosSparklesTrigger");
}

// Same exact-token rule for the secret walls the Lumos triggers unblock.
inline bool isSecretWallClassToken(const char* clsTok)
{
    return tokenEquals(clsTok, "GenericColObj") ||
           tokenEquals(clsTok, "KWBlockingVolume");
}

// Subclass-proof primitive: does a class chain (receiver class first, ending
// near Object) contain any of the wanted family class objects? Pointers only;
// callers pass REFS as the family class objects resolved by path at init
// (NULL entries are skipped, a chain stops at its first NULL/unknown link).
inline bool chainContainsAny(void* const* chain, int chainN,
                             void* const* refs, int refsN)
{
    if (!chain || !refs || chainN <= 0 || refsN <= 0) return false;
    for (int i = 0; i < chainN && chain[i]; ++i)
        for (int j = 0; j < refsN; ++j)
            if (refs[j] && chain[i] == refs[j]) return true;
    return false;
}

// Membership tests for the scan caches. fullName must be the mod's GetFullName
// rendering ("ClassToken Package.Object"). "Texture hgame.LumosTriggerIcon",
// "Class hgame.LumosTrigger" and the placed emitter
// "LumosSparklesEmitter HP3_InsideHub.LumosSparklesEmitter0" reject; the
// placed actor "LumosTrigger HP3_InsideHub.LumosTrigger3" accepts.
inline bool isLumosTriggerObject(const char* fullName)
{
    char tok[64];
    classTokenOf(fullName, tok, sizeof(tok));
    return isLumosTriggerClassToken(tok);
}

inline bool isSecretWallObject(const char* fullName)
{
    char tok[64];
    classTokenOf(fullName, tok, sizeof(tok));
    return isSecretWallClassToken(tok);
}

} // namespace hp3lumos

#endif // HP3_LUMOS_SYNC_H
