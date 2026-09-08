# HP3-SplitScreen

Split-screen multiplayer for **Harry Potter and the Prisoner of Azkaban** (PC, EA/KnowWonder 2004).

The retail game runs on Unreal Engine 2 with **every trace of split-screen support stripped out** — `SplitScreen`, `AddPlayer`, `RemovePlayer` and all joystick-axis symbols are missing from the shipped binaries, and the UnrealScript compiler is unreachable. This mod puts it back **without recompiling a single line of the game and without modifying any game file**.

A proxy `d3d8.dll` loads alongside the game, hooks the renderer, and drives the engine's own `UCanvas::DrawPortal` once per player. Each player gets an equal vertical strip following a different character; players 2..N are directly controllable with their own keyboard or gamepad.

## Features

- **N-player vertical split** — `Players=N` in `hp3mod.ini` produces N strips (2 and 3 verified; the view count is data, not code)
- **Player 2 (and 3) fully controllable**: walk, turn, jump, cast real spells, use/interact
- **Distinct characters per view** (Harry / Hermione / Ron), each with their own camera, floor and collision
- Real spellcasting through the game's own script VM (Rictusempra, Depulso, Lumos, ... ) with hold-to-aim, vertical aim, and (v51) player-1-faithful gameplay activation: statues, jump pads, doors and lesson objects react to player 2's spells
- Camera that slides against walls instead of clipping, gamepad camera-follow + orbit (v13+)
- Works on free-roam levels; survives level changes

## Install

1. Copy `bin/d3d8.dll` and `bin/hp3mod.ini` into the game's `system\` folder, next to `hppoa.exe`.
2. Run the game normally. No launcher, no patches, nothing else.

Windows loads a DLL from the application directory before the one in `System32`, so that is the whole install. Uninstall = delete the two files (plus `hp3mod.log` if present).

> Check the build: open `system\hp3mod.log` — line 2 must say `build v51`.

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

Any 32-bit MinGW-w64 toolchain works; the source is a single translation unit (`src/dllmain.cpp`) with an export table (`src/d3d8.def`). The small, platform-independent floor-handoff policy is in `src/cast_ground.h`. Runtime configuration lives in `src/hp3mod.ini` — the in-code fallbacks deliberately match the shipped defaults.

Run the host-side regression tests with `./tests/run.sh` (requires a native C++ compiler; override with `HOST_CXX`). These test the recovery policy, not HP3's native collision/animation implementation.

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
src/dllmain.cpp      mod / engine integration (v51)
src/cast_ground.h    testable post-cast floor-handoff policy
src/d3d8.def         Direct3DCreate8 export alias
src/hp3mod.ini       default configuration
bin/d3d8.dll         prebuilt v51 (zig/clang cross-build of src/dllmain.cpp; the GitHub Actions workflow produces the MinGW build)
bin/hp3mod.ini       shipped configuration
docs/MANUAL.txt      full in-game manual (all ini options, controls, troubleshooting)
tests/run.sh        host-side floor-handoff regression tests
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

## Disclaimer

Unofficial fan mod for personal use. It contains no game code and requires you to own a legal copy of the game. Not affiliated with or endorsed by EA, KnowWonder, or Warner Bros. Harry Potter and the Prisoner of Azkaban is a trademark of its respective owners.
