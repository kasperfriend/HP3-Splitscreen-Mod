# HP3-SplitScreen

Split-screen multiplayer for **Harry Potter and the Prisoner of Azkaban** (PC, EA/KnowWonder 2004).

The retail game runs on Unreal Engine 2 with **every trace of split-screen support stripped out** — `SplitScreen`, `AddPlayer`, `RemovePlayer` and all joystick-axis symbols are missing from the shipped binaries, and the UnrealScript compiler is unreachable. This mod puts it back **without recompiling a single line of the game and without modifying any game file**.

A proxy `d3d8.dll` loads alongside the game, hooks the renderer, and drives the engine's own `UCanvas::DrawPortal` once per player. Each player gets an equal vertical strip following a different character; players 2..N are directly controllable with their own keyboard or gamepad.

## Features

- **N-player vertical split** — `Players=N` in `hp3mod.ini` produces N strips (2 and 3 verified; the view count is data, not code)
- **Player 2 (and 3) fully controllable**: walk, turn, jump, cast real spells, use/interact
- **Distinct characters per view** (Harry / Hermione / Ron), each with their own camera, floor and collision
- Real spellcasting through the game's own script VM (Rictusempra, Depulso, Lumos, ... ) with hold-to-aim and vertical aim
- Camera that slides against walls instead of clipping, gamepad camera-follow + orbit (v13+)
- Works on free-roam levels; survives level changes

## Install

1. Copy `bin/d3d8.dll` and `bin/hp3mod.ini` into the game's `system\` folder, next to `hppoa.exe`.
2. Run the game normally. No launcher, no patches, nothing else.

Windows loads a DLL from the application directory before the one in `System32`, so that is the whole install. Uninstall = delete the two files (plus `hp3mod.log` if present).

> Check the build: open `system\hp3mod.log` — line 2 must say `build v49`.

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

The DLL builds and the policy tests pass; **v49 still needs in-game hardware confirmation**. If a hop remains, send the new log rather than increasing the old suppression timers.

### GitHub Actions release build

A preconfigured workflow (`.github/workflows/build-release.yml`) builds the same DLL and bundles the default `hp3mod.ini` into a GitHub release. To use it:

1. Open the **Actions** tab in the repository.
2. Select **Build release** in the left sidebar.
3. Click **Run workflow**.
4. Optionally enter a release **tag** and **title**, then run the job. Leave the tag blank to auto-generate a unique one (for example `v49-20260908-1234567890`).

The workflow compiles the DLL natively on a Windows runner using MSYS2's 32-bit MinGW-w64 toolchain (`mingw-w64-i686-gcc`), attaches both the `.zip` and the individual `d3d8.dll` / `hp3mod.ini`, and also uploads them as run artifacts.

## Repository layout

```
README.md            this file
FINDINGS.txt         the reverse-engineering findings (engine internals, offsets,
                     the cast pipeline, animation/physics quirks, dead ends)
build.sh             build script (32-bit MinGW)
src/dllmain.cpp      mod / engine integration (v49)
src/cast_ground.h    testable post-cast floor-handoff policy
src/d3d8.def         Direct3DCreate8 export alias
src/hp3mod.ini       default configuration
bin/d3d8.dll         prebuilt v49
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

## Disclaimer

Unofficial fan mod for personal use. It contains no game code and requires you to own a legal copy of the game. Not affiliated with or endorsed by EA, KnowWonder, or Warner Bros. Harry Potter and the Prisoner of Azkaban is a trademark of its respective owners.
