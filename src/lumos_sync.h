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
// v68: the player-near-the-WALL-actor fallback radius/height. A large
// secret wall's own Location can sit far from the surface a player presses
// against, so this test alone is unreliable; v68 primarily asks the wall's
// PAIRED TRIGGER (stock's own radius check) and keeps this as a secondary.
static const float WallActorNearRadius  = 350.0f;
static const float WallActorNearHeight  = 150.0f;
// v68 retry cadences (ms) for the level-side field resolution, the
// nothing-found-yet level scan, and a trigger dispatch that found no
// receiver. All replace "one-shot at first call" or "latch the failure
// forever" semantics that silently disabled the Lumos replication on
// hardware.
static const std::uint32_t ResolveRetryMs   = 1000;
static const std::uint32_t EmptyScanRetryMs = 1000;
static const std::uint32_t DispatchRetryMs  = 1000;

// ---------------------------------------------------------------------------
// v71: WHERE A REPLICATED LUMOS LIGHT HAS TO SIT (the "light underneath"
// field report). Stock rides the light with baseWand.Tick ->
// TheLumosLight.UpdateLocation(WandEndPoint), which SetLocations BOTH the
// light and its Particles. On hardware that ride never reached a companion
// wand, so v68..v70 seeded the light at the pawn and parked it there - the
// field report's "light effect underneath, no wand glow": a bright light and
// its LumosLightFX particles sitting on the character's body instead of at
// the wand tip. The mod now rides both onto a wand-tip anchor itself.
// ---------------------------------------------------------------------------

// Fallback anchor when the wand actor's own Location is not believable:
// hand height above the pawn's origin. This is the game's own projectile
// origin (hp3charged::LaunchPlan org = pawn + 45Z, verified in the field
// since v63 - spells leave the hand, not the feet), and the split camera
// pivots at pawn + 50Z, so the pawn's Location is at its feet in HP3.
static const float WandTipFallbackZ   = 45.0f;
// Sanity bounds for trusting the WAND ACTOR's own Location as the tip.
// The lower bound is the important one: a wand actor sitting AT the pawn's
// origin (dZ = 0, the shape that made the light read as "underneath" on
// hardware) is not a wand tip, it is an actor the game never positioned -
// hand height above the pawn is the better answer in that case.
static const float WandTipMaxHoriz    = 256.0f;
static const float WandTipMinZ        = 16.0f;    // at/below the feet = bogus
static const float WandTipMaxZ        = 128.0f;   // above the head    = bogus
// Ride tolerance: below this the light is close enough and the mod leaves
// it alone (so a STOCK ride that is working is never fought).
static const float LightRideEpsilon   = 8.0f;
// A light this far from its pawn is LOST (parked at the level-entry wand
// position, at the world origin, ...). Only the recovery path uses it, and
// only after the light has been proven stationary for LightRideHealMs.
static const float LightLostDist      = 250.0f;
static const std::uint32_t LightRideHealMs = 1000;
// v71: a light the mod owns is force-retired after the stock window plus a
// grace period, whatever stock's own Tick did or did not do. The v70 log
// never shows a companion light going cold; without this ceiling a
// replicated light can burn forever ("stuck spell on him").
static const std::uint32_t ModLightHardOffMs = 33000;

// ---------------------------------------------------------------------------
// v71: THE WALL-OPEN KEY THAT ACTUALLY MATCHES HP3 (see FINDINGS 28).
// The v70 hardware log killed the "Event -> wall.Tag" theory for the level
// under test: HP3_InsideHub's LumosTrigger0 has Event=0x8D1A and the ONLY
// actor carrying Tag 0x8D1A is LumosSparklesTrigger5 (whose own Event is
// 0x0, so the chain ends there). The cached secret walls carry Tags
// 0x3E63 / 0x405A - nothing links them to a trigger by name at all, and the
// mod's single proximity pick ("wall #63") was not what the player was
// bumping into. So the open rule no longer tries to derive the stock
// linkage: it keys on the one thing the level designer DID tune - the
// trigger's own radius, the stock InLumosRadius check - and then opens
// whatever actually blocks near it.
// ---------------------------------------------------------------------------
// Any blocking actor within this radius of an ARMED trigger (one a tracked
// player currently stands inside) is opened. Stock's own fDistanceCheck
// default is 512, i.e. exactly the designer's "you are at this wall"
// distance; the mod uses the trigger's own value when it reads one.
static const float BlockerTriggerRadius = 512.0f;
// ...or within this of a player who is himself inside an armed trigger.
// This is the "whatever she is actually bumping" key: it catches a blocker
// whose actor origin sits far from the trigger (a large volume) and, unlike
// the v66.5 wall-actor test, it is class-agnostic.
static const float BlockerPlayerRadius  = 200.0f;
static const float BlockerPlayerHeight  = 160.0f;

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
//           visible wand glow; TurnOn() then Particles.EnableEmission(True)
//           and stores it in LumosLight.Particles
//   The wand's own Tick then calls TheLumosLight.UpdateLocation(WandEndPoint)
//   every frame while TheLumosLight.bLumosOn (UpdateLocation SetLocations
//   BOTH the light and its Particles property - the glow rides the wand tip
//   through the light's Particles pointer), and LumosLight.Tick auto-TurnOff()s
//   after fLumosTimeToTurnOff = 30s
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

// v70: a light the mod lit may only be OBSERVED as "auto-off'd by stock"
// when it was lit through the STOCK TurnOn script (stock Tick owns it). A
// v68-fallback light deliberately holds bLumosOn low until a glow exists, so
// reading it "cold" one frame later is the mod's own doing, not the stock
// 30 s timer - v69 latched such a light dead after ONE frame.
inline bool observedStockAutoOff(bool modLit, bool stockLit, bool burning)
{
    return modLit && stockLit && !burning;
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

// v69: THE STOCK TRIGGER->WALL LINKAGE. Stock LumosTrigger fires
// TriggerEvent(Event, self, None) and the ENGINE dispatches that Event to
// every live actor whose Tag equals it (Engine.Actor.TriggerEvent walks
// GObjObjects comparing Name fields). So the DEFINITIVE proof that a wall
// is the secret wall of a given trigger is the raw name-index equality
// wall.Tag == trigger.Event - not distance. Both values are plain 4-byte
// name indexes in this engine (Engine.Actor.Tag / Engine.Actor.Event), so
// the comparison is two guarded dword reads, no string work. A wall linked
// by Event is a secret wall no matter how far away its actor Location sits
// from the trigger (a large wall's centre can sit >900 units out, which
// silently failed the v66.5 SecretWallPairRadius pairing and left the
// wall unclassified, hence never opened by the SetCollision fallback).
// wallTag==0 (NAME_None) never links - an unlinked wall stays ordinary.
inline bool wallEventLinksToTrigger(unsigned wallTag, unsigned trigEvent)
{
    return wallTag != 0 && trigEvent != 0 && wallTag == trigEvent;
}

// v66.5: the only open condition. secretCandidate=false keeps every
// ordinary GenericColObj/KWBlockingVolume solid - the v66.x rule opened ANY
// cached wall within 300 units of ANY player while Lumos was on.
inline bool wallShouldBeOpen(bool lumosActive, bool secretCandidate,
                             bool playerNear)
{
    return lumosActive && secretCandidate && playerNear;
}

// v68: the open condition with STOCK's own proximity key. playerNearTrigger
// means a tracked player stands inside the wall's PAIRED LumosTrigger radius
// (the exact stock InLumosRadius check, measured from the trigger, not from
// the wall actor - a large wall's Location can sit far from the surface a
// player presses against); playerNearWall is the v66.5 wall-actor proximity
// fallback. Either proves the player is at the wall the stock trigger would
// have opened for PlayerHarry.
inline bool wallShouldOpenProxied(bool lumosActive, bool secretCandidate,
                                  bool playerNearTrigger, bool playerNearWall)
{
    return lumosActive && secretCandidate &&
           (playerNearTrigger || playerNearWall);
}

// ---------------------------------------------------------------------------
// v71 policy: the wand-tip anchor and the trigger-keyed wall opening.
// ---------------------------------------------------------------------------

// Is the wand actor's own Location a believable wand tip for this pawn? A
// wand actor whose Location was never updated by the game (still at the
// level's origin, at the pawn's feet, or hundreds of units away - all seen
// on hardware) must not be used as the glow anchor, or the "wand glow" ends
// up under the floor or across the map.
inline bool wandLocIsTip(const float pawnLoc[3], const float wandLoc[3])
{
    if (!pawnLoc || !wandLoc) return false;
    if (wandLoc[0] == 0.0f && wandLoc[1] == 0.0f && wandLoc[2] == 0.0f)
        return false;                                   // unassigned vector
    float dx = wandLoc[0] - pawnLoc[0];
    float dy = wandLoc[1] - pawnLoc[1];
    float dz = wandLoc[2] - pawnLoc[2];
    if (dz < WandTipMinZ || dz > WandTipMaxZ) return false;
    return (dx * dx + dy * dy) <= (WandTipMaxHoriz * WandTipMaxHoriz);
}

// The point a Lumos light (and its Particles glow) must sit at. The wand
// actor's own Location when it is believable, otherwise hand height above
// the pawn - the same origin the game fires its own spells from.
inline void wandTipFor(const float pawnLoc[3], const float wandLoc[3],
                       float out[3])
{
    if (!out || !pawnLoc) return;
    out[0] = pawnLoc[0];
    out[1] = pawnLoc[1];
    out[2] = pawnLoc[2] + WandTipFallbackZ;
    if (wandLoc && wandLocIsTip(pawnLoc, wandLoc)) {
        out[0] = wandLoc[0];
        out[1] = wandLoc[1];
        out[2] = wandLoc[2];
    }
}

// Does this light need to be moved onto the tip? Deliberately loose: a
// STOCK ride that is working keeps the light within a few units of the
// wand tip, so the mod never fights it.
inline bool lightNeedsRide(const float lightLoc[3], const float tip[3],
                           float eps = LightRideEpsilon)
{
    if (!lightLoc || !tip) return false;
    float dx = lightLoc[0] - tip[0];
    float dy = lightLoc[1] - tip[1];
    float dz = lightLoc[2] - tip[2];
    return (dx * dx + dy * dy + dz * dz) > (eps * eps);
}

// Is this light LOST - so far from its pawn that no ride can be carrying
// it? Only the recovery path (lead's own light, never touched otherwise)
// uses this, and only together with the stationarity test in dllmain.cpp.
inline bool lightIsLost(const float lightLoc[3], const float pawnLoc[3],
                        float dist = LightLostDist)
{
    if (!lightLoc || !pawnLoc) return false;
    float dx = lightLoc[0] - pawnLoc[0];
    float dy = lightLoc[1] - pawnLoc[1];
    float dz = lightLoc[2] - pawnLoc[2];
    return (dx * dx + dy * dy + dz * dz) > (dist * dist);
}

// Is this light UNDERFOOT - hugging the pawn's origin (at or below the feet)
// instead of being carried at hand height? The field report's "heavy light
// underneath" is exactly this shape: a light the ride parks at the pawn's
// origin. The mod lifts it onto the wand tip when LeadRide=1.
static const float LeadUnderfootZ = 16.0f;
static const float LeadUnderfootR = 150.0f;
inline bool lightIsUnderfoot(const float lightLoc[3], const float pawnLoc[3],
                             float z = LeadUnderfootZ, float r = LeadUnderfootR)
{
    if (!lightLoc || !pawnLoc) return false;
    float dx = lightLoc[0] - pawnLoc[0];
    float dy = lightLoc[1] - pawnLoc[1];
    float dz = lightLoc[2] - pawnLoc[2];
    return dz < z && (dx * dx + dy * dy) <= (r * r);
}

// v71: the ONE open rule for every blocking actor near a Lumos secret wall.
//   lumosActive     - the shared Lumos window is open
//   playerAtTrigger - a tracked player stands inside the trigger's own
//                     radius (stock's InLumosRadius key). This is the only
//                     thing that proves the player is at a secret wall at
//                     all, and it is what keeps ordinary railings and
//                     camera blockers elsewhere in the level solid.
//   eventLinked     - the actor's Tag equals the trigger's Event (the stock
//                     TriggerEvent dispatch would have reached it: the
//                     definitive "this is the wall" proof, kept from v69)
//   nearTrigger     - within BlockerTriggerRadius of that armed trigger
//   nearPlayer      - within BlockerPlayerRadius/Height of that player (the
//                     "whatever you are actually bumping into" key)
inline bool blockerShouldOpen(bool lumosActive, bool playerAtTrigger,
                              bool eventLinked, bool nearTrigger,
                              bool nearPlayer)
{
    if (!lumosActive || !playerAtTrigger) return false;
    return eventLinked || nearTrigger || nearPlayer;
}

// v71: a revealed secret wall STAYS open. Stock opens it permanently -
// LumosTrigger fires once (bFirstEventSent, never reset) and the wall's
// TriggerToggle mover has bEventLeaving = False - so v66.5..v70 re-closing
// a wall when the Lumos window expired (or when the player stepped two
// metres back) took the secret away again and could shut it on a player
// standing inside it.
inline bool blockerStaysOpen(bool openedByMod, bool latchOpen)
{
    return openedByMod && latchOpen;
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

// v69: EXACT class-token equality against the trigger classes the stock
// games place at secret walls. Ground truth (HP2 decompile, HP3 inherits):
// Triggers/LumosTrigger.uc and Triggers/LumosSparkles.uc - the sparkles
// class is named "LumosSparkles", NOT "LumosSparklesTrigger" (the v66.4
// rule rejected the real placed class if HP3 ships that name, emptying the
// trigger cache). "LumosSparklesEmitter" (an Engine.Emitter subclass - the
// second 2026-09-09 GPF receiver), "LumosTriggerLarge", "LumosLight",
// "LumosSpell" and every other look-alike still reject here. A true
// subclass of the trigger family re-enters via the dllmain.cpp class-chain
// proof (lumosChainProves, pointer comparison against the resolved family
// class objects and the v69 Engine.Triggers family anchor), never through
// a name.
inline bool isLumosTriggerClassToken(const char* clsTok)
{
    return tokenEquals(clsTok, "LumosTrigger") ||
           tokenEquals(clsTok, "LumosSparklesTrigger") ||
           tokenEquals(clsTok, "LumosSparkles");
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
