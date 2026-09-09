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
- **Cooperative sustained-cast (v58, fires on release)**: if **any** character — Player 1's engine cursor lock, or a Player 2/Player 3 held cast — keeps the same **cooperative-family** cast target (a `CompanionSpellTrigger`-type object — the game's own trio puzzle — or a class certified by the strict all-three observation) for **more than 10 seconds**, the real Harry/Hermione/Ron enter the shared hold and **nothing fires at the interval**. When the holder **releases**, the trio fires exactly once: the holder's own natural shot plus one natural companion release each, three real spells on the shared target, counted as the cooperative spell by its own script. Ordinary objects (pumpkins, spawners) never arm the trio, so a casual 10-second hold no longer hijacks Ron's AI; borrowed AI heroes also hold still instead of fighting their controller. Also fixes the hard crash (GPF in `UObject::GetPathName <- GetFullName`) when entering Hogwarts / changing levels: cached actors are validated against the live object table before any name or field use, and the level-change cache drop runs every frame.
- Camera that slides against walls instead of clipping, gamepad camera-follow + orbit (v13+)
- Works on free-roam levels; survives level changes

## Install

1. Copy `bin/d3d8.dll` and `bin/hp3mod.ini` into the game's `system\` folder, next to `hppoa.exe`.
2. Run the game normally. No launcher, no patches, nothing else.

Windows loads a DLL from the application directory before the one in `System32`, so that is the whole install. Uninstall = delete the two files (plus `hp3mod.log` if present).

> Check the build: open `system\hp3mod.log` — line 2 must say `build v56`.

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

Any 32-bit MinGW-w64 toolchain works; the source is a single translation unit (`src/dllmain.cpp`) with an export table (`src/d3d8.def`). The testable floor-handoff policy is in `src/cast_ground.h`; aiming geometry is in `src/aim_policy.h`, the cooperative hold lifecycle is in `src/coop_cast.h`, and the native visual adapter is in `src/native_aim.h`. Runtime configuration lives in `src/hp3mod.ini` — the in-code fallbacks deliberately match the shipped defaults.

Run the host-side regression tests with `./tests/run.sh` (requires a native C++ compiler; override with `HOST_CXX`). These test recovery, aim geometry, continuous cooperative-hold identity/timing, and native-effect lifecycle using an engine double—not HP3's actual VM, collision, animation, or rendering.

### v58 cooperative sustained cast (any character's >10s hold; fires on release; Ron's AI restored)

This is a **general fallback for genuine three-character sustained-cast interactions**, not a Pixie-well shortcut. How the stock game holds the "hovering triple cast" (established from the HP2-family script source and the v56 hardware ProcessEvent capture): the engine player's held cast plus the `SpellCursor` lock (`LockOn(Actor TargetActor)` — which also runs `ChooseSpell` **from the target's own vulnerable spell class**) is what recruits the companions; their controllers enter the held cast on their own (~700 ms later in the capture) and the trigger's own script decides when the converged holds complete it.

v58 applies that same effect when **any single character** holds the same *cooperative-family* target for more than 10 seconds — and v57's hardware test drove two major fixes:

- **any character, any mode**: Player 1's engine cursor lock *or* a Player 2/3 held cast on the same cooperative target (a `CompanionSpellTrigger`-chain object, or a class the strict all-three observation certified this session) arms the shared hold after `CoopCastHoldMs`. v57 armed for **any** castable candidate, so every casual long hold on a pumpkin/spawner force-borrowed the AI heroes — the Ron "run back and forth to cast together" report — plus its split-ON proof certification from any 1.2 s P1 cursor lock self-poisoned the class table. v58 re-gates to the class family, **not** the structurally-unreachable v54–v56 behaviour-proven gate, and a long lock on an ordinary object no longer certifies its class.
- **fires on release, never on the interval (v57 auto-launch fix)**: the borrowed heroes are no longer fed `PressedFire` (on the engine pawn that entered the real fire pipeline — wind-up `StateCast` then `finalizeSpell`/`SpawnSpell` ~250 ms later with no release input; the mod's own P2 hold provably holds `StateCasting` with `StartCasting(cls,1.0)`/`playCastAim` alone). When the holder releases, each borrowed hero releases **once** by a natural `ReleasedFire` *while genuinely in the held cast state* — the stock companion release — so three real spells converge on the shared target, counted by its script. There is no `StopCasting`-before-release and no same-frame `currentSpell` restore any more (that v57 pair was the "shouts, plays the animation, doesn't shoot" on every repeat: the engine finalize re-reads `currentSpell` ~300 ms after the release trigger).
- **borrowed AI heroes hold still** (Ron fix, part two): the AI controller keeps pushing movement every tick while State holds the cast pose, so `coopMaintain` now pins the XY velocity/acceleration of each mod-borrowed, AI-driven hero during the hold.
- **flicker tolerance everywhere**: up to 250 ms of lost target (cursor `None` frames for P1, aim-pick gaps for P2/P3) resets neither the pre-arm dwell nor an already-armed hold; only a real release (> grace) ends it, as the trio fire. A hard target change/deletion, another player's normal cast, level travel or F10 cancels silently (plain `StopCasting`, no `ReleasedFire` noise events).
- it still never invokes generic `Trigger`, synthetic `Touch`, repeated projectile impacts, or a guessed completion field — the target's own script decides the result.

`CoopCastFallback=0` disables the trio hold; `CoopCastHoldMs` is clamped to a minimum/default of `10000`. Keep `CastGameplay=1`. Because this deliberately reaches the previously avoided P1 controller route, test it first on a save you can reload and send `hp3mod.log` if anything behaves differently from the stock P1 interaction.

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
zig c++ -target x86-windows-gnu -O2 -shared -static \
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

## Disclaimer

Unofficial fan mod for personal use. It contains no game code and requires you to own a legal copy of the game. Not affiliated with or endorsed by EA, KnowWonder, or Warner Bros. Harry Potter and the Prisoner of Azkaban is a trademark of its respective owners.
