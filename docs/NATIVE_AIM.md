# v53: original HP3 seeking and hovered-spell effects

## Status and scope

Implemented and cross-built on 2026-09-08. Host tests pass, including the
production visual adapter driven by an engine double. **Not yet run in HP3 on
Windows/D3D8 hardware.** Those tests cannot establish native VM compatibility,
particle instancing, visibility, or visual parity on the user's machine.

The v51 hardware log confirmed working P2 spell selection, homing, and object
activation, but showed an additive sprite cycling Sparkle_1/3/7 rather than the
original hovered-spell effect. v52 replaced that visual path. It retains the
v51 projectile/ProcessTouch/Touch and cast-exit/floor-recovery implementation.

The v52 hardware log then showed the native seek particles working but the
locked SpellGesture never spawning: every Depulso/Spongify target logged "no
readable default.SpellIcon", so the legacy Sparkle_1/3/7 marker drew over the
native effect for the whole hold (glowing like P1 only after a successful cast
changed hover state). v53 fixes the icon read itself - see "v53 icon
resolution" below.

`[actions] NativeAim=1` is the default, even in an existing INI without the key.
`NativeAim=0` restores the legacy visual/range policy for comparison. It does
**not** restore unsafe borrowing of P1's cursor. `GlowStyle=6` is the recommended
legacy fallback. `GlowSize` only sizes that fallback, not the original particles.

## What the actual game packages establish

The supplied `Core.u`, `Engine.u`, `kwGame.u`, and `HGame.u` are uncompressed
package version 129, licensee version 0. Their source TextBuffers are stripped;
this research reads compiled function tokens, property tags, and native export
names. It is not a claim to have recovered complete UnrealScript source.

| Component | Original behavior relevant to this change |
|---|---|
| `KWGame.SelectCursor` | Camera ray length is `fLOS_Distance + dot(cameraDir, pawn.Location - cameraStart)`; the stock picker iterates `TraceActors` with a one-unit extent. |
| `HGame.SpellCursor` | Overrides the base range from 1500 to **770**; spawns a separate `SpellGesture` and projector. It is a hidden controller actor, not an idle marker to borrow. |
| `SpellCursorEmitter` | An `Engine.Emitter`, not a sprite. The seeking template uses Sparkle_3 with the game's color, size, lifetime, acceleration, and respawn settings. `DoTargeting(true)` enables particle respawning. |
| `SpellCursorEmitter.lockOn` | Sets owner, calls `Kill`, then returns. The later mesh/radius code is unreachable. Calling this method does **not** produce the hovered-object aura. |
| `SpellGesture` | An `Engine.Emitter` with two private `SpriteEmitter` subobjects: a gesture sprite and spherical gesture sparkles. `ChangeGesture` assigns **both** textures from `spellClass.default.SpellIcon`. |
| `SpellGesture.SetReadyToCast` | Sets `bHidden = !bReady`, and the sparkle sphere's minimum/maximum radius from the target's CollisionRadius and 1.1 times that radius, bounded by the particle class defaults. |
| Gesture placement | Uses the rotated `CentreOffset`; cylinder offset distance is `1.1 * CollisionRadius * SizeModifier + 2 + GestureDistance`. The gesture faces opposite the line-of-sight direction. The original cursor also smooths its motion. |
| Readiness | `SpellCursor.canCast` delegates to `HPHeroController.canCast`, which delegates to the controlled pawn's `canCast`. P2 must query its own pawn, not P1's controller. |
| Target validity | Stock code requires live `vulnerableToClass`, `bProjTarget`, `bCollideActors`, and `HasSpell`; rejects inactive Triggers and respects `fMaxDistanceForReaction` for HPPawn/SpellTrigger. |

Both native emitter classes default to PHYS_Trailer (10). P1 has a separate
cursor actor for them to follow. A directly driven P2 effect must instead use
PHYS_None; otherwise the engine would drag it back to the owning pawn.

Public references were useful for navigation, but **not authoritative for HP3
particles**: [HP2UScriptDecompile](https://github.com/metallicafan212/HP2UScriptDecompile)
uses a different ParticleFX architecture; the
[Shrek2-LevelEditor](https://github.com/Master-64/Shrek2-LevelEditor) exports
contain KWGame stubs. The
[HarryPotterUnrealWiki resources](https://github.com/metallicafan212/HarryPotterUnrealWiki/wiki/Main-Resources)
list inspection tools. Actual supplied HP3 bytecode supersedes guesses based on
those neighboring games. The baseline implementation is merged PR #8 / commit
`070d456f806d908856d44c87bf88600ab0eaabb5`.

## Implementation and safety boundaries

- `src/native_aim.h` is included by the existing single translation unit. It
  spawns a **mod-owned** SpellCursorEmitter while seeking, or SpellGesture while
  locked. It never rewrites P1's cursor, controller, or particle defaults.
- Reflection validates the effect functions and parameter types/offsets/bool
  masks before calling `ChangeGesture`, `SetReadyToCast`, and `DoTargeting`.
  SpellIcon comes from `UClass::GetDefaultObject`, not a guessed texture list.
  Native FX bool masks come from the exported `UBoolProperty::CopySingleValue`
  applied to scratch words (all-one source, zero destination), not the older
  "first power of two after Offset" heuristic. Disassembly of the supplied
  Core.dll confirms this reads the actual mask and only writes the destination.
  This is important for hiding and detecting dead particles. The older gameplay
  bool-mask helper is left unchanged to keep this change scoped.
- The spawned emitter must contain readable SpriteEmitter instances distinct
  from its class defaults before scripts index them. There is a one-second
  initialization allowance; then it falls back to the legacy marker until the
  next hold. Missing icons, failed spawn/placement, or invalid ownership also
  fall back, without restarting StartCasting.
- Effect position/rotation use native `SetLocation`/`SetRotation`, not just
  memory writes that leave render bounds or scene registration behind.
- World positions stay valid between ticks. Per-pane `bHidden` masking replaces
  the old practice of moving native emitters to Z=-100000. **Actual per-pane
  rendering still needs hardware verification.**
- FX identity is checked against the object-table slot, class, full name, current
  map, and deletion flag. Effects are retired on seek/lock/target/spell changes,
  release, split shutdown, level reset, character cycling, and AI-control handoff.
- While hovering, a spell change updates P2's ChooseSpell without restarting the
  cast. Changes in readiness or target radius update the original gesture
  function. Where the no-argument `canCast` return ABI is available, readiness is
  queried on P2; otherwise an already selected target is treated as ready.
- Actor/candidate/cursor discovery now filters against the camera's active map.
  It no longer selects HP_preamble actors while playing in Save0. Diagnostics
  read ByteProperty fields as bytes and distinguish particle UObjects from
  Actors; an empty diagnostic scan no longer "proves" a HUD-only cursor.

### Deliberate differences from the full original cursor

This is a native **visual adapter**, not a second player controller or a full
port of SpellCursor. Calling the whole original lockOn function would invoke
P1/global-controller gameplay logic and risk the working P2 casting.

The original 770-unit, camera-relative range and rotated targeting centre are
shared by aiming and the existing begin/release picker. This intentionally
reduces v51's 3200-unit reach. Live vulnerability, projectile-target and actor
collision flags are checked. The successful v51 forgiving target corridor and
FastTrace visibility checks remain; this is **not** the original TraceActors
selection/hysteresis. The free marker uses a refined FastTrace world-surface
probe, so non-target dynamic occluders can differ from the original trace.

The remaining stock HasSpell/progression, per-target reaction-distance, and
inactive-Trigger checks are **not fully ported**. In particular, introducing P1's
wand/progression assumptions could remove the companion spell access that v51
made work. This adapter also does not duplicate the original cursor's motion
smoothing, projector, lock sounds, or all target-shape-specific placement rules.
Native particles and spell-specific icons are reused; pixel-identical behavior
has not been established. These limitations should not be mistaken for full
original-player targeting parity.

## Reproducing the investigation locally

Use your legally owned game packages in an ignored directory, for example
`dist/research/system/`. Do not commit, package, or redistribute them. The tools
under `tools/hp3/` contain only the reader code and require Python 3.8+.

```bash
python3 tools/hp3/bytecode.py dist/research/system/HGame.u SpellGesture
python3 tools/hp3/bytecode.py dist/research/system/HGame.u SpellCursorEmitter
python3 tools/hp3/bytecode.py dist/research/system/HGame.u SpellCursor.IsValidTarget
python3 tools/hp3/bytecode.py dist/research/system/kwGame.u SelectCursor
python3 tools/hp3/properties.py dist/research/system/HGame.u SpellCursor --first particleType
python3 tools/hp3/properties.py dist/research/system/HGame.u SpellGesture --first Emitters
python3 tools/hp3/properties.py dist/research/system/HGame.u SpellGesture.GestureSprite
python3 tools/hp3/properties.py dist/research/system/HGame.u SpellGesture.GestureSparkles
```

The bytecode reader is partial and fails on unsupported tokens. Its left-hand
addresses are **serialized byte offsets**; jump operands are **VM offsets**
(compact object references expand in memory). Do not equate them to determine
control flow. Property inspection requires a unique defaults block ending at
the object's end; array payloads are deliberately left as hex.

SHA-256 of the packages/native library used, to distinguish different retail builds:

```text
Core.u   cb6a84a03279b9f6c597c296b43a89b5d865e406e4654866c55216b3b0982b39
Engine.u 3b5c2b2002dbc8522e8a932e2733228f7f60711d55a9ccb1a033621b3a12f1ef
kwGame.u 9ee25ea5d804d9892a23276ef9e1bf4e5c4dfc452ad66aa09895fd0d6abb61f3
HGame.u  d0981cc5fc704391e3f152f1e94295b2e0440b2660b2b5027dba7eb2cd889ef1
Core.dll b22cc27f6454eff97909ec0aa1b54a0c7aecdef2c5202e762a245ef431f92bbc
```

## Validation and hardware acceptance checklist

Run `bash tests/run.sh`: the unchanged 14 floor-handoff cases, aim geometry and
identity assertions, and the production native adapter with an engine double.
The latter exercises seek/lock transitions, icon swaps, changing radius and
readiness, independent P2/P3 effects, pane masks, release, recycled slots, stale
maps, delayed particle initialization failure, placement/spawn failure, and the
per-hold fallback latch. It **does not execute UnrealScript or render pixels**.

The research reader has six synthetic tests: `python3 tests/hp3_package_test.py`.
No game data is needed for them. The adapter also passes an AddressSanitizer /
UndefinedBehaviorSanitizer host run.

The supplied `bin/d3d8.dll` is an x86 Windows Zig 0.13 cross-build, as was v51's
prebuilt binary. Its export includes undecorated `Direct3DCreate8`. Its imported
DLL set matches the previous binary (including the UCRT API sets); this is not a
new compatibility claim for older Windows installations. GitHub Actions retains
the repository's separate 32-bit MinGW build route.

On the same save used for the v51 log:

1. Replace `system/d3d8.dll`. Keep custom controls; add `NativeAim=1` under
   `[actions]` if desired (default on). Keep `CastGameplay=1`, `AimedCast=0`.
2. Confirm `build v53` and `[nativeaim] bindings OK` in `hp3mod.log`.
3. Hold P2 cast away from targets, then over the Spongify pad and Depulso
   triggers. Expect `[nativeaim] p2 SEEK`, then `LOCK` with the correct
   material name (plain or wet/shader texture) and `privateParticles=2`, not
   just a cycling generic sparkle. If a target still falls back, the log now
   says exactly what sits in `default.SpellIcon` (class, slot offset, value) -
   include that line in the report.
4. Compare the native seeking glow and hovered-spell aura side by side with P1.
   Approach/leave the range boundary; change targets during a single hold.
5. Aim simultaneously as P1/P2 (and P3 if available). P1 must remain unchanged;
   P2/P3 effects must not leak into another pane or leave long particle trails
   from the void. Test walls, close objects, camera pitch, and rotated targets.
6. Release on the pad/triggers. Check that the same v51 spell/hit handlers still
   activate them and that cast pose/movement recover as before. Test rapid
   recasts, jumping, stairs, and walking after casting.
7. Toggle F10 off/on while holding; cycle characters; load/travel to another
   level. No orphaned glow, crash, or HP_preamble target should remain.
8. If native particles are absent, use `NativeAim=0` for an A/B comparison and
   send the v53 log plus a screenshot of both panes. Tap D while P1 holds aim to
   include the corrected live emitter/gesture diagnostic.

`[nativeaim] p2` means the second human player. Historical `[castgame] p1`
messages still use the older zero-based indexing and mean that same player.
