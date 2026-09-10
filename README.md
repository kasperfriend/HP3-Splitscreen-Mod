# HP3-SplitScreen

Split-screen multiplayer for **Harry Potter and the Prisoner of Azkaban** (PC, EA/KnowWonder 2004).

The retail game runs on Unreal Engine 2 with **every trace of split-screen support stripped out** — `SplitScreen`, `AddPlayer`, `RemovePlayer` and all joystick-axis symbols are missing from the shipped binaries, and the UnrealScript compiler is unreachable. This mod puts it back **without recompiling a single line of the game and without modifying any game file**.

A proxy `d3d8.dll` loads alongside the game, hooks the renderer, and drives the engine's own `UCanvas::DrawPortal` once per player. Each player gets an equal vertical strip following a different character; players 2..N are directly controllable with their own keyboard or gamepad.

## Features

- **N-player vertical split** — `Players=N` in `hp3mod.ini` produces N strips (2 and 3 verified; the view count is data, not code)
- **Player 2 (and 3) fully controllable**: walk, turn, jump, cast real spells, use/interact
- **Distinct characters per view** (Harry / Hermione / Ron), each with their own camera, floor and collision
- Real spellcasting through the game's own script VM (Rictusempra, Depulso, Lumos, ... ) with hold-to-aim, vertical aim, and (v51) player-1-faithful gameplay activation: statues, jump pads, doors and lesson objects react to player 2's spells
- **Native HP3 aim effects (v52/v53)**: original seeking particles and target-sized hovered spell icons; hardware verification pending.
- **Holder-origin virtual-trio charge (v65, fires on release)**: hold a cast on a cooperative-family target for 8 seconds to arm x3 power. The interval never auto-fires. On release, all three spells leave from the holder in the same direction: P2/P3 bonuses copy the normal shot’s exact projectile kinematics, while P1 uses one shared holder-origin fallback. Bonus `Instigator` labels still let the trigger observe three distinct casters. The mod never borrows companions’ controllers, cast state, or AI. When P2/P3 arms the charge, the holder’s one floating wet-shader spell is joined by two companion wet-spell gestures that replicate the original per-hero join exactly: each icon sits at the stock gesture distance `1.1*CollisionRadius*SizeModifier + 2 + GestureDistance` on **its own hero's** line of sight (`target.Location + CentreOffset + Normal(heroLoc - targetLoc) * dist`) and faces that hero, so the three icons fan out on their own heroes' sides; each is chased with the stock `SpellGesture.MoveSmooth((goal-loc)*rate*dt)` motion — snapped once at lock, then gliding in the air after its hero (rate 10 while locked, 8 during a target flicker; P1 already shows the stock 1-to-3 look through its own cursor/companions). While P1 charges, AI companions that are actually casting are held still (movement stats pinned with save/restore) so Ron stops his run/cast/run oscillation yet keeps walking to range; everything restores on unlock.
- Camera that slides against walls instead of clipping, gamepad camera-follow + orbit (v13+)
- Works on free-roam levels; survives level changes

## Install

1. Copy `bin/d3d8.dll` and `bin/hp3mod.ini` into the game's `system\` folder, next to `hppoa.exe`.
2. Run the game normally. No launcher, no patches, nothing else.

Windows loads a DLL from the application directory before the one in `System32`, so that is the whole install. Uninstall = delete the two files (plus `hp3mod.log` if present).

> Check the build: open `system\hp3mod.log` — line 2 must say `build v65`.

## Player 2 controls

| Key | Action |
|---|---|
| `T` / `G` | walk forward / back |
| `F` / `H` (or `Q`/`E`) | turn left / right |
| `R` | jump |
| `Y` | cast spell (hold to aim, release to fire) |
| `V` | use / interact |

The numpad mirrors the same actions (NumLock ON), and XInput gamepads (Xbox/DualSense) are supported: left stick walks/turns, right stick is the camera-follow/orbit + vertical spell aim. Full details in `docs/MANUAL.txt`.

## Building from source

```bash
# Debian/Ubuntu
sudo apt install g++-mingw-w64-i686
./build.sh          # -> dist/d3d8.dll
```

Any 32-bit MinGW-w64 toolchain works; the source is a single translation unit (`src/dllmain.cpp`) with an export table (`src/d3d8.def`). The testable floor-handoff policy is in `src/cast_ground.h`; aiming geometry is in `src/aim_policy.h`, the cooperative hold lifecycle is in `src/coop_cast.h`, the stock three-float gesture placement is in `src/coop_trio.h`, charged-projectile planning is in `src/charged_cast.h`, and the native visual adapter is in `src/native_aim.h`. Runtime configuration lives in `src/hp3mod.ini` — the in-code fallbacks deliberately match the shipped defaults.

Run the host-side regression tests with `./tests/run.sh` (requires a native C++ compiler; override with `HOST_CXX`). These test recovery, aim geometry, continuous cooperative-hold identity/timing, charged-projectile trajectories, and native-effect lifecycle using engine-independent policy code—not HP3's actual VM, collision, animation, or rendering.

### v65 exact stock per-hero gesture placement

v64 placed each companion icon at the **holder's** camera front point and nudged
it sideways by a small fixed spacing; when the two companion heroes were near
each other their directions projected together and the two icons collapsed onto
one another (the user-visible "two companions overlapping" bug). v65 removes
that fabrication and reproduces the original cooperative join **per hero**:

- every joined hero owns one `SpellGesture` whose goal is **its own** `vLOS_End`
  — `target.Location + CentreOffset + Normal(heroLoc - targetLoc) *
  (1.1 * CollisionRadius * SizeModifier + 2 + GestureDistance)` — the point at
  the stock gesture distance on that hero's own line of sight (`heroGoal` in
  `src/coop_trio.h`);
- each gesture faces opposite its owner's line of sight (target → hero), so
  the three icons naturally fan out on their own heroes' sides instead of
  sharing the holder's front point;
- each icon is still chased with the stock `SpellGesture.MoveSmooth((goal-loc)*
  rate*dt)` motion — snapped once at lock like `LockOn.SetLocation`, then
  gliding after its own hero (rate 10 while the target is still a possible
  target, rate 8 on the cached centre during the 250 ms target flicker);
- a companion hero unavailable for a frame keeps gliding on its last goal.

The holder's own native-aim LOCK gesture is unchanged (it already shows the
correct central icon). Purely visual: the mod still owns only its gesture
actors and never touches companion controllers, cast state, target/spell
fields, or AI. The 8-second arm and once-on-release x3 firing are unchanged.

### v64 stock cooperative gesture motion (superseded)

v63 showed the armed charge as three wet-shader spell icons but pinned the two
companion icons **beside the target** at the target plane. v64 moved every icon
to hover at the stock gesture distance in **front** of the target and added the
stock `MoveSmooth` chase, but anchored both companions at the holder's camera
front point with a fixed lateral spacing — the two icons overlapped whenever the
companions' directions projected together. v65 replaces that placement with the
exact per-hero goal described above.

### v63 8-second arm + three floating wet spells

The charged-x3 hold now arms after an **8-second** continuous hold (was 10) on a cooperative-family target — the new `CoopCastHoldMs` minimum/default is 8000 ms. Nothing still fires at the threshold; the charge only arms, and x3 fires once on release from the holder, exactly as v62.

When **P2 or P3** arms the charge, the holder’s single floating wet-shader spell is joined by two companion wet-spell gestures near the cooperative target — the same “one floating spell becomes three” look as stock P1’s held cast plus its AI companions joining. P1 keeps that look through the stock cursor/companion-join path, so the overlay is added only for mod-driven holders (never doubled). The companion gestures are visual only: they never touch companion controllers, cast state, target/spell fields, or AI, and they change nothing about what fires on release.

### v62 holder-origin virtual-trio charged x3 cast

v61 launched its two virtual companion shots from the companions’ own positions. At long separation that sent the remote shots into nearby walls instead of toward the holder’s target. v62 makes the holder authoritative for the whole volley. On P2/P3 release, each bonus copies the normal holder projectile’s exact `Location`, `Velocity`, `Rotation`, `DesiredRotation`, acceleration, projectile speeds and physics before the next engine tick. P1’s normal projectile remains engine-owned, so shot 2 receives a holder-origin target-directed launch and shot 3 clones shot 2.

The two bonus `Instigator` values are still labelled as the other heroes because `CompanionSpellTrigger` counts distinct casters. Those heroes now provide identity only: their positions, directions, controllers, target fields, cast state and AI are untouched. The behavior is symmetric whichever of P1, P2 or P3 completes the hold.

### v61 virtual-trio charged x3 cast

v60 hardware proved the trigger counts distinct casters, not same-pawn hits: P2’s fully delivered same-caster x3 still did not complete a `CompanionSpellTrigger`, and P1’s solo bonuses never delivered at all (the split-OFF frame returned before the delivery tick ran). v61 fires a virtual trio instead: the holder’s normal cast stays shot 1, while shots 2 and 3 launch from the other two trio members’ wand positions at staggered speeds (100% / 75%) with their `Instigator` fields labelled to those companions. No companion is borrowed — controllers, cast state, target/spell fields, and AI are untouched; only the two new spell actors are written. The delivery tick now also runs with split OFF.

The stock-join guard is state-aware: only companions that are actually casting (`IsAimingOrCasting`, else a held `currentSpell`) have `GroundSpeed`/`AccelRate` pinned with save/restore, so the hold survives the engine AI tick that overwrote v60’s impulse-only zeroes; companions still walking to range are left alone. Pins release per slot on unlock, reset, or level travel.

### v60 single-caster charged x3 cast

The stock cooperative interaction is recognized by a sustained lock on the game’s cooperative target family (`CompanionSpellTrigger` and verified subclasses). The mod waits for `CoopCastHoldMs` (minimum/default 8 seconds) and only **arms** an x3 charge; it does not fire at the threshold.

When the holder releases, the normal cast remains shot 1. Two additional calls to the game’s own `HPPawn.SpawnSpell(class, target)` are made on the **same player pawn**, producing shots 2 and 3 with the same spell class and target. Both bonus actors are detached from the caster, moved outside the caster’s collision body, aimed at the target’s real center, given projectile physics/velocity, and assigned independent hit-delivery watches. P2’s ordinary shot also retains its target candidate and watch. This fixes v59’s stationary red light and ensures all three spell deliveries can reach the target, including when P1 is the holder.

No other hero is borrowed: the mod does not write companion target/spell fields, call companion `StartCasting`/`ReleasedFire`, replace controller links, or force P1’s cursor. The unmodified game itself can recruit AI companions as soon as P1 locks a `CompanionSpellTrigger`, before the eight-second mod threshold. During that P1 hold only, v60 zeros horizontal velocity/acceleration on companions that remain AI-controlled; this prevents Ron’s stock run/cast/run loop without changing his controller, follow setup, spell fields, or release events. Normal AI movement resumes immediately after P1 unlocks.

Target loss, deletion, level travel, or split shutdown cancels the armed bonus silently. `CoopCastFallback=0` disables this behavior. Keep `CastGameplay=1`.

### v57 level-travel crash fix

Entering Hogwarts (or any level change) as any character could hard-crash with `UObject::GetPathName <- UObject::GetPathName <- UObject::GetFullName <- FPlayerSceneNode::Render`. `GetFullName` is what the mod's object-naming helper calls, and cached actor pointers (pawns, cursors, camera, aim FX, coop heroes) outlived the level that owned them: `IsBadReadPtr` happily passes on freed-but-still-mapped UObject memory, and the name walk then follows the destroyed `Outer` chain into memory the new level already reuses. v57 adds a liveness check against the engine's own `GObjObjects` table at every cached-pointer dereference, refuses to name or destroy anything that is not a live object, and runs the level-change cache invalidation every frame instead of every 45th.

### v52 native aiming glow (needs hardware verification)

The supplied original HP3 packages show that seeking and hovering use **different
actors**: `SpellCursorEmitter` and `SpellGesture`. The hovered effect sets both
particle textures from the selected spell's `default.SpellIcon` and sizes its
sparkle sphere to the target. v51 only showed a generic additive sparkle.

v52 uses those original effect classes and script functions for P2+, without
borrowing P1's cursor or changing v51's projectile/hit and cast-recovery path.
It also uses the original **770-unit camera-relative range** and ignores stale
actors from previous maps. The forgiving v51 lock selector remains; this is not
a complete port of the original controller/targeting rules.

- Replace `system/d3d8.dll`; existing INIs default to `[actions] NativeAim=1`.
- Keep `CastGameplay=1`, `AimedCast=0`. `NativeAim=0` restores the legacy
  sprite/range policy for comparison; `GlowSize` only affects that fallback.
- Look for `[nativeaim] bindings OK`, then `p2 SEEK` and `p2 LOCK` with a
  spell-specific Texture and `privateParticles=2`. Check P1/P2 simultaneous
  aiming, the Spongify pad and Depulso triggers, release, F10, and map travel.
- Cross-build and host tests pass. **Actual D3D8 visibility and per-pane particle
  isolation have not been tested here.** Failed initialization falls back to the
  old marker until release. This is not yet a claim of pixel-identical visuals.

See [native aim research, limitations, and test checklist](docs/NATIVE_AIM.md).
For the same optional Zig 0.13 cross-build used for the prebuilt binary:

```bash
mkdir -p dist
zig c++ -target x86-windows-gnu -O2 -s -shared -static \
  -o dist/d3d8.dll src/dllmain.cpp src/d3d8.def -lwinmm -Wall -Wextra
```

### v49 post-cast movement check

The v48 hardware log confirmed the pose fix, but its saved-height dip guard only **deferred** the fall until walking resumed. v49 keeps the pose cleanup and instead reacquires floor contact through the engine's native physics path, then leaves walking physics on briefly to validate it before idle can freeze it again. It never restores a saved height or repeatedly resets falling to walking.

To upgrade from v48, replace `system\d3d8.dll`; you can keep your existing INI with `[actions] NoDip=1` (the default). Test casting while stationary, walking immediately after a cast, and waiting at least five seconds before walking. Also check walking during a cast, rapid re-casts, jumps, stairs and ledges. The log now reports `[castground]` **reacquire**, **verified**, or **released**, including Base and Floor values. `NoDip=0` disables the new handoff and selects the legacy animation-only fallback.

The floor-handoff policy tests pass; **v49–v51 still need in-game hardware confirmation**. If a hop remains, send the new log rather than increasing the old suppression timers.

### v51 cast gameplay (statues, jump pads, doors)

v50's activation never fired on real objects: it only recognised actors literally named `SpellTrigger`, fired the default spell with no target, and its fallback was a plain `Trigger()` that spell objects ignore. v51 replicates what the original player's cast actually does (confirmed against the KnowWonder KWGame framework and the HP2 script):

1. **Castable objects are found by their own properties**, not by name — the engine-level `vulnerableToClass`-style reaction property, `SizeModifier`/`CentreOffset` targeting data and the object's `HandleSpell<Name>` handlers. A one-time index of the object table plus a calibrated class chain (`UObject::Class` / `UStruct::SuperField`, verified on a known pawn before use) makes this work with zero hard-coded offsets.
2. **The spell is chosen from the target**, exactly like the wand's `ChooseSpell(target.vulnerableTo…)`: a Lapifors statue gets Lapifors, a pad gets Spongify. Players 2+ never had a spell menu, so this is the only way they can ever cast the right spell.
3. **The projectile keeps its target** (the game's own homing) and is aimed at the object's targeting centre.
4. **When it arrives, the game's own dispatch runs**: the spell's `ProcessTouch(target, hitLocation)` (the wand's autohit path) or, for trigger-family objects, `Touch(spell)`. Direct handler calls are only a fallback when neither exists. Recovered signatures are logged as `[castgame-fn]`.
5. While aiming, the glow **locks onto the castable object** it will fire at, like player 1's cursor.

Diagnostics: `[castgame-scan]` lists what is castable in the level and why; `[castgame]` shows the target, the chosen spell, the flight and the handler that ran; player 1's own casts are now watched too (`PLAYER 1 spellTarget/currentSpell/cursor aCurrentTarget`) and open a short `[pelog]` window that logs the real activation events (`ProcessTouch`, `HandleSpell*`, `Touch`, `OnBounce`…), so a hardware log can prove whether player 2 follows the same path. `[actions] CastAutoHit=0` leaves activation to engine collision only. Hardware confirmation pending — if a statue still ignores player 2, send the log with a player-1 cast on the same object in it.

### GitHub Actions release build

A preconfigured workflow (`.github/workflows/build-release.yml`) builds the same DLL and bundles the default `hp3mod.ini` into a GitHub release. To use it:

1. Open the **Actions** tab in the repository.
2. Select **Build release** in the left sidebar.
3. Click **Run workflow**.
4. Optionally enter a release **tag** and **title**, then run the job. Leave the tag blank to auto-generate a unique one (for example `v51-20260908-1234567890`).

The workflow compiles the DLL natively on a Windows runner using MSYS2's 32-bit MinGW-w64 toolchain (`mingw-w64-i686-gcc`), attaches both the `.zip` and the individual `d3d8.dll` / `hp3mod.ini`, and also uploads them as run artifacts.

## Repository layout

```
README.md            this file
FINDINGS.txt         the reverse-engineering findings (engine internals, offsets,
                     the cast pipeline, animation/physics quirks, dead ends)
build.sh             build script (32-bit MinGW)
src/dllmain.cpp      mod / engine integration (v56)
src/cast_ground.h    testable post-cast floor-handoff policy
src/aim_policy.h     testable camera/radius/ownership helpers
src/coop_cast.h      testable continuous cooperative-hold identity/timing policy
src/native_aim.h     original HP3 effect adapter (included by dllmain.cpp)
src/d3d8.def         Direct3DCreate8 export alias
src/hp3mod.ini       default configuration
bin/d3d8.dll         prebuilt v54 binary (this PR changes src only; the GitHub Actions workflow rebuilds bin/d3d8.dll into the v56 release)
bin/hp3mod.ini       shipped configuration
docs/MANUAL.txt      full in-game manual (all ini options, controls, troubleshooting)
docs/NATIVE_AIM.md  package findings, limitations, hardware acceptance checklist
tools/hp3/          read-only local package/bytecode inspection tools
tests/run.sh        host-side recovery, aim geometry, and adapter regression tests
```

## Version history (short form)

- **v13** jump / physics pin proven on hardware
- **v23** ProcessEvent telemetry — the engine can be watched thinking
- **v37** per-event timeline logging; the post-cast hop named in the log
- **v39–v43** cast-exit storm swallow, animation re-sync, ground physics hold
- **v44** settle radius 350 + standing yaw lock (hardware-confirmed)
- **v45** first hardening attempt — regressed on hardware, pulled
- **v47** the post-cast animation re-sync rebuilt event-driven: it arms once at the cast state's own exit event and executes only when nothing new (player or mod) has started acting on the pawn — a re-sync can no longer interrupt a cast no matter how fast you chain them
- **v48** one recovery run per cast and a reliable pose/facing re-pick; the saved-height dip guard was later shown to postpone the hop until movement
- **v49** native floor-contact handoff instead of the saved-height/physics-byte loop; walking is validated before idle freezes physics, with no default falling/landing animation suppression. New aim and level/pawn changes discard stale recovery state. Hardware confirmation pending.
- **v50** cast gameplay targeting for players 2..N: a release ray-selects SpellTrigger/cast objects under the camera line, passes the target through playCast/castSpell/SpawnSpell, and directly calls the target spell/trigger handler as a safety net so objects activate like player 1 casts.
- **v51** cast gameplay rebuilt on the original cast path: castable objects found by their reaction/targeting properties and spell handlers (statues, jump pads, doors), spell class chosen from the target, projectile keeps its target, and the game's own spell `ProcessTouch` / trigger `Touch` runs the object's handler on arrival. Aim glow locks onto the castable object; player 1's casts are logged for comparison.

- **v52** original HP3 seeking particles and spell-specific SpellGesture hover aura for P2+, with reflected icon/radius/readiness calls, 770-unit camera-relative range, current-map actor lookup, per-player effect ownership, and regression tests. Native VM/visual hardware confirmation pending.
- **v53** resolves hovered SpellGesture icons on each chosen spell's class chain and accepts the game's wet/shader glyph materials instead of rejecting them as plain textures.
- **v54** behavior-certifies genuine P1-led three-character cast classes live, then gives a P2/P3 player who continuously holds such a target for more than 10 seconds a scoped P1-cursor/trio-held-cast bridge. It restores all temporary state on every cancellation boundary and does not use generic trigger/touch/completion shortcuts. In-game verification pending.
- **v55** fixes the v54 regression where a split-ON session never certified a class (the observer only ran while split was OFF), so a >10-second P2/P3 hold fell back to ordinary single casts. Certification now also runs live from P1's genuine stock cursor lock during a split session; the castable scan admits handler-only objects per object instead of suppressing them whenever any other class in the level declares a vulnerable property.
- **v56** adds a Player-1 holder path and 250ms cursor-flicker tolerance. A single uninterrupted >10-second Player-1 cursor lock on an object now fires the three-character cooperative hold directly (P1's own cast goes through the engine; the mod borrows Hermione and Ron alongside), so a deliberate P1 hold on a CompanionSpellTrigger class enters the shared hold without depending on a separate P2/P3 >10s attempt. Brief None frames between same-target frames (<=250ms) no longer reset the cert dwell or the 10s fire dwell; a real release (>250ms) re-arms both. The P2/P3 >10s hold path is unchanged.
- **v57** removes the hardware-unreachable certification gate from the cooperative hold: ANY single character (P1 cursor lock or P2/P3 held cast) holding the same game-verified cast target for >10 seconds arms the trio hold directly. Heroes the game itself already recruited are borrowed untouched, the P1 `SpellCursor.LockOn` bridge is best-effort, and P2/P3 holds get the same 250ms flicker grace. It also fixes the hard level-travel GPF (entering Hogwarts): every cached actor is validated against the live `GObjObjects` table before any name/field/destroy use, and the level-change cache drop runs every frame instead of every 45th. `bin/d3d8.dll` is refreshed to match the source banner (it had been stale since v54).
- **v58** fixes both hardware-test findings on v57's trio: (1) the hold no longer auto-launches at 10 s — borrowed heroes are never fed `PressedFire` (that event put the engine pawn through the real fire pipeline), and when the holder *releases*, each borrowed hero fires exactly one natural `ReleasedFire` shot from the genuine held-cast state (three converging spells counting as the cooperative cast) — with no `StopCasting`-first and no same-frame `currentSpell` restore, fixing the repeat "shouts but doesn't shoot" desync; (2) Ron's AI keeps behaving: arming is re-gated to the genuine cooperative class family (`CompanionSpellTrigger`-chain objects or session-certified classes — casual 10 s holds on pumpkins/spawners no longer borrow the AI heroes), split-ON proof certification from a plain P1 cursor lock is family-restricted, and borrowed AI heroes get their movement push pinned for the duration of the hold. Post-arm target flicker (≤250 ms) no longer tears down an armed hold. The v57 level-travel crash fix is retained unchanged.
- **v59** replaces companion borrowing with a same-caster x3 release. The 10-second threshold only arms the charge; release produces the holder’s normal projectile plus two `SpawnSpell` projectiles from the same pawn. No companion fields, controllers, cast states, movement, or P1 cursor are touched.
- **v60** completes v59’s raw `SpawnSpell` bonuses: each is moved clear of the holder, aimed, given velocity/projectile physics, and watched in its own delivery slot for every holder (including P1). P2/P3’s original target watch is retained as well, so x3 means three effective deliveries rather than one normal shot plus stationary red actors. If P1’s stock cursor recruits an AI Ron/Hermione during the cooperative lock, only that AI pawn’s horizontal movement impulses are suppressed until unlock, stopping the stock run/cast/run oscillation without borrowing the companion or changing controller/cast/release state.
- **v62** launches the entire virtual trio from the holder: P2/P3 bonuses clone the normal projectile’s exact kinematics, P1’s two bonuses share one holder-origin plan, and companion pawns contribute only distinct `Instigator` labels—remote companions can no longer send shots into walls.
- **v61** fires a virtual trio instead of same-caster x3 (hardware showed the trigger counts distinct casters): bonus shots launch from the other trio members’ positions at staggered speeds with companion `Instigator` labels, the delivery tick runs with split OFF so P1’s solo bonuses land, and the Ron guard pins only actually-casting companions’ `GroundSpeed`/`AccelRate` with save/restore so the hold survives the AI tick while walkers keep walking.
- **v63** arms the charged x3 after an 8-second continuous hold (the `CoopCastHoldMs` default/minimum is now 8000) and, for P2/P3 holders, shows the stock cooperative visual: the holder’s one floating wet-shader spell is joined by two companion wet-spell gestures so the armed charge reads as three floating spells (P1 already shows it through the stock cursor/companion-join). The companion gestures are visual only and change nothing about what fires on release.
- **v64** replaces the pinned companion gestures with the stock motion: all three icons hover at the stock gesture distance in front of the target, companion icons sit on their own heroes’ sides, and every icon is chased with `SpellGesture.MoveSmooth((goal-loc)*rate*dt)` (snap once at lock, then glide) — the holder’s native-aim LOCK gesture gets the same chase, the companions survive the 250 ms target flicker, and the pure placement policy lives in `src/coop_trio.h` with host tests.
- **v65** fixes the v64 overlap: the two companion gestures no longer share the holder’s camera front point (pushed sideways by a fixed spacing). Each now uses the exact stock per-hero goal — `target.Location + CentreOffset + Normal(heroLoc - targetLoc) * (1.1*CollisionRadius*SizeModifier + 2 + GestureDistance)` — and faces its own hero, so the three wet icons fan out on their own heroes’ sides and glide after them with the stock `MoveSmooth` chase (rate 10 locked / 8 flicker).
- **v66** adds the Spongify air yaw-rotation unlock (the mid-air `g_viewYaw = g_jumpYawLock` overwrite is removed so horizontal aim/yaw is free during jumps and Spongify bounces) and Lumos secret-wall passability replication (active `TheLumosLight` is synchronised across companion wands, nearby `LumosSparklesTrigger`/`LumosTrigger` fire, and `GenericColObj`/`KWBlockingVolume` walls unblock so any player walks through revealed walls).
- **v66.1** fixes the two v66 regressions found in the field. (1) The v66 Lumos tick performed a full `GObjObjects` scan with `GetFullName` on every object every rendered frame (multiple scans per frame, split on or off — so even the menu lagged): trigger/wall/wand/light lookups are now cached per level, the heavy scans run once per level (re-armed on level change or when a cached pointer stops being live), and the fallback scans are throttled to 1 Hz. (2) A driven companion left parked in `PHYS_Falling` with no real fall after the Hogwarts door transition (flat Z, zero vertical velocity) is handed back `PHYS_Walking` through the native after a 600 ms flat-Z grace window, re-acquiring its floor instead of staying frozen while every jump is refused as “airborne”.- **v66.3** fixes the Player-2 Lumos crash. The per-level Lumos trigger/secret-wall scan admitted any object whose whole `GetFullName` string contained the token — including the UClass entries (`Class hgame.LumosTrigger`) and the spell's HUD asset `Texture hgame.LumosTriggerIcon` the engine loads alongside Lumos. With Lumos active the trigger loop then ran `KWGame.KWPawn.Trigger` on that Texture — the exact field GPF `UObject::ProcessEvent <- (Texture hgame.LumosTriggerIcon, Function KWGame.KWPawn.Trigger) <- FPlayerSceneNode::Render`, because a live-texture receiver passes every guard (liveness, `IsBadReadPtr` on the shared UObject arena) and garbage floats at actor field offsets can pass the proximity test. Cache membership is now decided by the object's **class token** only (`src/lumos_sync.h`, regression-tested in `tests/lumos_sync_test.cpp` with the exact crash names), scan-time rejections are counted and logged, and — defense in depth against a cached pointer being recycled into a different object mid-level — the `Trigger()` fire site and the wall collision-write site re-verify the receiver's class through the calibrated `UObject::Class` chain (guarded dword reads only, no `GetFullName`) before running code on it or writing to it.
- **v66.2** fixes the two v66.1 regressions found in the field. (1) The stuck companion is now actually unstuck: v66.1's repair was gated on the standing-jump latch being clear, and that latch only clears once the pawn *leaves* `PHYS_Falling` — which a pawn parked by the Hogwarts door transition never does, so the repair ran 15 times in 20 s, moved nothing, and left the jump button permanently dead. Recovery is now a bounded escalation (`src/stuck_pawn.h`): clear the leaked latch, then lift up to 3×, then re-seat beside the character it follows up to 2×, then back off — tunable in the new `[recovery]` ini section, `Unstick=0` disables it. (2) The remaining FPS cost was object liveness, not the Lumos scans: `cgLiveObject` walked all 8820 `GObjObjects` entries *and* ran `IsBadReadPtr` over the whole table on every single call, dozens of times per frame. It is now an O(1) hash snapshot rebuilt at most once per 16 ms (`src/live_index.h`), dropped on level travel and whenever the mod destroys an actor. Alongside it: `getPawn` resolves once per frame instead of four times, `actorInCurrentLevel` no longer re-derives the camera's full name per call, the castable scan filters on the cached class before building any full name, the unresolved-`TheLumosLight` fallback sweep uses `GetName` instead of `GetFullName`, and Lumos stops rewriting every secret wall's collision bits every frame while it is off.
- **v66.4** fixes the *second* Player-2 Lumos crash, this time with v66.3 running — same HP3_InsideHub gargoyle, GPF history `UObject::ProcessEvent <- (LumosSparklesEmitter HP3_InsideHub.LumosSparklesEmitter0, Function KWGame.KWPawn.Trigger) <- FPlayerSceneNode::Render`. Two defects cooperated: v66.3's "class-token" gate moved the substring match but did not remove it (`strstr` *within* the class token), so the placed secret-wall sparkles actor class `LumosSparklesEmitter` still entered the trigger cache (the hub scan line even showed "21 triggers"), passing the fire-time chain re-verify's identical substring behind an `isActor` gate that an `Emitter` trivially passes; and — deeper — the fire site called one globally resolved `KWGame.KWPawn.Trigger` on whatever the cache held, so any admitted non-pawn executed pawn bytecode with a too-small `this`. Fix, three layers: (1) cache membership is now **exact** class-token equality (`src/lumos_sync.h`: only `LumosTrigger`/`LumosSparklesTrigger` and `GenericColObj`/`KWBlockingVolume`, no substring anywhere, regression-tested with the exact crash names); (2) a differently-named *subclass* of one of those — the only reason the substring existed — is admitted exclusively by chain pointer proof (`lumosChainProves` walks the receiver's calibrated `UObject::Class` → `UStruct::SuperField` chain with guarded dword reads and pointer-compares against the path-resolved family class objects), at scan admission and at every fire/write verification; (3) the `Trigger()` fire site never replays a function found on a different class again: with the chain verified it fires the most-derived `Trigger` declaration in the **receiver's own** chain (`CgClass::fnTrigger`, already mapped by the castgame index), falling back to `Engine.Actor.Trigger`, and skips + logs `[lumos] v66.4 BLOCKED Trigger()` when neither can be proven — even a future admission bug can no longer run foreign bytecode on a live object.
- **v66.5** fixes the three findings of the v66.4 field report (P2 Lumos at the gargoyle: “the wand disappears, spell works, but I still can't go through wall” + `UnOctree.cpp:1598` assertion on quit). **One root cause** behind the solid wall *and* the quit crash: the wall replication wrote the `bCollideActors`/`bBlockActors`/`bBlockPlayers` bits directly into wall actors, but this engine resolves collision through the **collision octree**, which only the native `Engine.Actor.SetCollision` updates — a poked wall stayed in the octree and kept blocking, and the cleared flag on an octree member tripped `check(Actor->bCollideActors)` during `FCollisionOctree` teardown at quit. Every wall transition now goes through the engine's own native (`execSetCollision` resolved from Engine.dll exports, driven with the same synthesized bool bytecode tokens the DrawPortal call uses), so an opened wall is genuinely removed from the octree — passable for real, for every player — and the octree/flag pair stays consistent at every instant, which is what removes the quit crash even mid-spell. Pristine per-wall bits are snapshotted at scan time and restored exactly on close and on level travel (`lumosRestoreAllWalls`), only walls **paired to a lumos trigger** (≤900 units, `wallShouldBeOpen` policy) are ever opened, and the feature self-disables if the native is missing. **Satellite fixes:** the per-frame `ShowWeapon` call (a zeroed-parm *hide* in this lineage) is replaced by writing `bHidden=false` directly on each pawn's Wand — P2's wand stays out and lit; the wand-light lookup now matches the exact `LumosLight` class by `Owner == wand` (stock spawns it owned by the wand — the old `Base == wand` fallback could never match, making the whole light sync a silent no-op when `TheLumosLight` was unresolved) and applies the stock `TurnDynamicLightOn` values without ever touching the light's own collision; the state timer follows a genuinely burning light in 1-second extend-only ratchets (tick-wrap safe); and the proven-no-op `Trigger()` fire loop is removed (the stock `OnLumosOn` broadcast already armed the triggers when the gargoyle lit).


## Disclaimer

Unofficial fan mod for personal use. It contains no game code and requires you to own a legal copy of the game. Not affiliated with or endorsed by EA, KnowWonder, or Warner Bros. Harry Potter and the Prisoner of Azkaban is a trademark of its respective owners.
