// ---------------------------------------------------------------------------
// hp3mod - split-screen for Harry Potter and the Prisoner of Azkaban (UE2).
//
// Injection: hppoa.exe / D3DDrv.dll import Direct3DCreate8 from d3d8.dll.
// A proxy d3d8.dll next to the exe wins the loader search order on Windows.
//
// PHASE C: N-way VERTICAL split-screen via UCanvas::DrawPortal.
//
//   * DrawPortal(X,Y,W,H, CamActor, CamLocation, CamRotation, FOV, ClearZ)
//     is native(480) - it renders a complete, correctly-projected scene into
//     an arbitrary sub-rect of the currently locked canvas. Covering the
//     screen with N side-by-side portals therefore yields a true N-way
//     vertical split with no engine-internal surgery, and generalises to any
//     N by construction.
//
//   * It is a native, so it reads its arguments from UnrealScript bytecode
//     via Stack.Step(). We synthesise a tiny bytecode buffer. The opcode
//     numbers are NOT hardcoded: Core.dll exports every constant handler
//     (execIntConst, execObjectConst, ...), so we scan the GNatives dispatch
//     table for each handler's address and recover its real opcode byte.
//
// Verified layout facts (empirical, this build):
//   UObject                 = 0x2C bytes
//   UPlayer::Actor          = Viewport+0x34
//   UViewport::SizeX/SizeY  = Viewport+0x8C / +0x90   (read 1280 / 720)
//   AActor::Location        = Actor+0x150 (FVector)
//   AActor::Rotation        = Actor+0x15C (FRotator)
//   FFrame                  = { vtable@0, Node@4, Object@8, Code@0xC }
//   GNatives dispatch       = Core.dll ?GNatives@@3PAP8UObject@@AEXAAUFFrame@@QAX@ZA
// ---------------------------------------------------------------------------
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <mmsystem.h>
#include <stdlib.h>
#include "cast_ground.h"
#include "aim_policy.h"
#include "coop_cast.h"

// ------------------------------- config ------------------------------------
// Number of split-screen views. Nothing below assumes 2.
static int g_numPlayers = 2;
static float g_camDist = 125.0f, g_camHeight = 50.0f;
static int   g_camPitch = -1400;
static BOOL  g_camCollide = TRUE;
static BOOL  g_aimedCast  = FALSE;
static BOOL  g_nativeAim = TRUE;      // v52: original HP3 emitter + SpellGesture
static BOOL  g_castGameplay = TRUE;   // v50: make P2+ casts activate spell/gameplay targets, not only animate
// v54/v55: after a deliberate continuous hold on a class proven live by the
// game's own behavior (split-ON: P1's stock cursor lock; split-OFF: the stock
// P1-plus-companions route), mirror the normal three-character hold state.
// This is intentionally opt-in by time rather than a generic trigger bypass.
static BOOL  g_coopCastFallback = TRUE;
static DWORD g_coopCastHoldMs = hp3coop::DefaultHoldMs;
static int   g_glowSize   = 36;       // v24: -40% (was 60) by user request -
static BOOL  g_aimSup[8];             // v24 body-clearance: glow hidden this frame
                                      // the sparkle's original (hardware:
                                      // "a bit big"; DrawScale did nothing)
static int   g_glowStyle  = 6;        // v23: 6=sprite additive (default -
                                      // the only variant PROVEN visible on
                                      // the user's hardware; v22's borrowed
                                      // SpellCursor rendered in Wine only),
                                      // 0=legacy sprite fallback (never borrow P1),
                                      // 8=v20 factory particle (hw-invisible)
static BOOL  g_cleanupLight = FALSE;  // v21: light pose-cleanup chain (hop
static int   g_cleanupChain = 0;      // v25 hop bisect: 0=full(v23 chain, hw
                                      // pose-PROVEN) 1=noanimend(v24, hw HANG)
                                      // 2=min 3=exitanim(exit+AnimEnd only)
static int   g_cleanupDelay = 1200;   // v25: ms after release before the chain
                                      // v28/v29/v32/v33 hw verdict, condensed:
                                      // the state REFUSES to self-exit, the
                                      // split chain made it WORSE (snap at A,
                                      // dip+snap at B, pose still stuck), and
                                      // waiting for a self-exit only held the
                                      // pose longer. The single-pass full
                                      // chain (v23/v27) is the best hw state.
                                      // v48 keeps that call set verbatim and
                                      // only fixes WHEN it runs (see below).
static BOOL  g_bodyClear = TRUE;      // v27 isolation knob (v24 delta)
static BOOL  g_panePark  = TRUE;      // v27 isolation knob (v25/v26 delta)
                                      // A/B for hardware)
static BOOL  g_freeLook   = FALSE;  // v13: 0 (default) = right stick TURNS pawn+camera
                                    // like the original player; 1 = free orbit camera
static int   g_toggleKey  = VK_F10;
static BOOL  g_fileToggle = FALSE;   // "split_on" file trigger: harness only
static BOOL  g_strafeMode  = FALSE;  // 0 = left/right TURN like original player
static float g_turnSpeed   = 180.0f; // deg/s at full deflection
static float g_fovScale    = 1.0f;
static int   g_fovMode     = 1;      // 1 = preserve vertical FOV (unsqueezed)
static BOOL  g_padJump[8]  = {0};
static BOOL  g_padCast[8]  = {0};
static BOOL  g_padUse[8]   = {0};
static BOOL  g_wasMoving[8] = {0};

// Player 2..N key map. Defaults deliberately avoid every key the game already
// binds in defuser.ini: the arrow keys are TURNLEFT/TURNRIGHT/aArrowUp and
// therefore drive PLAYER ONE, which is why arrows appeared to "only control
// Harry". The numpad is completely unbound in the shipped config, so player 2
// lives there; player 3 uses I/J/K/L, also unbound.
struct PlayerKeys {
    int fwd, back, left, right, jump, cast, use, release, hard, inval;
    // Second binding for the same action. Player 2 gets the numpad AND a
    // letter cluster, because with NumLock OFF Windows reports the numpad as
    // the arrow keys - which the game binds to player 1, making player 2 look
    // dead while player 1 walks around.
    int fwd2, back2, left2, right2, jump2, cast2, use2;
    int turnL, turnR;
};
static PlayerKeys g_pk[8];

static void initKeyDefaults(void)
{
    memset(g_pk, 0, sizeof(g_pk));
    // player 2 -> numpad (requires NumLock ON, or Windows reports the arrows)
    g_pk[1].fwd  = VK_NUMPAD8; g_pk[1].back  = VK_NUMPAD2;
    g_pk[1].left = VK_NUMPAD4; g_pk[1].right = VK_NUMPAD6;
    g_pk[1].jump = VK_NUMPAD0; g_pk[1].cast  = VK_DECIMAL;
    g_pk[1].use  = VK_ADD;     g_pk[1].release = VK_SUBTRACT;
    g_pk[1].hard = 'M';        g_pk[1].inval = 'N';
    // NumLock-proof alternates for player 2 (all unbound in the shipped game)
    g_pk[1].fwd2 = 'T'; g_pk[1].back2 = 'G';
    g_pk[1].left2= 'F'; g_pk[1].right2= 'H';
    g_pk[1].jump2= 'R'; g_pk[1].cast2 = 'Y'; g_pk[1].use2 = 'V';
    g_pk[1].turnL = 'Q'; g_pk[1].turnR = 'E';        // deliberate turning only
    // player 3 -> I / J / K / L cluster
    g_pk[2].fwd  = 'I'; g_pk[2].back  = 'K';
    g_pk[2].left = 'J'; g_pk[2].right = 'L';
    g_pk[2].jump = 'U'; g_pk[2].cast  = 'O'; g_pk[2].use = 'P';
}

static int iniKey(const char *sect, const char *key, int def, const char *ini)
{
    int v = GetPrivateProfileIntA(sect, key, -1, ini);
    return (v > 0 && v < 256) ? v : def;
}


static BOOL keyDown(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000) != 0; }
static BOOL g_splitOn = FALSE;   // runtime toggle (F10 / split_on file)

// ------------------------------- logging -----------------------------------
#define MOD_BUILD  "v58"
#define MOD_STAMP "build v58 - 2026-09-09 - v58 TRIO FIRES ON RELEASE + RON AI FIX: the >10-second shared hold NO LONGER auto-launches when the interval completes. Borrowed heroes are no longer force-fed PressedFire (on the engine pawn Harry that entered the real fire pipeline - StateCast, finalizeSpell, SpawnSpell - 150-250 ms after arming with no release input; the mod's own P2 hold path proves StartCasting+playCastAim alone enter and hold the cast). When the holder RELEASES, the borrowed heroes fire exactly ONCE via a natural ReleasedFire while genuinely inside the game's held cast state (the stock companion release) - three near-same-frame real spells converge on the shared target, which its own script counts as the cooperative cast. No StopCasting-before-release and no same-frame currentSpell/spellTarget restore on fired heroes any more (those two produced the observed 'shouts, plays the animation, doesn't shoot' on every trio after the first). Cancellations (target changed/deleted, another player cast, level travel, split off) silently StopCasting the borrowed heroes with no ReleasedFire noise. RON AI: the trio re-gates to the genuine cooperative class family (CompanionSpellTrigger-type objects, or classes certified by the strict all-three observation this session) - v57's any-castable admission let every casual 10-second hold on a pumpkin/spawner force-borrow the AI heroes; split-ON proof certification from P1's plain cursor lock is likewise family-restricted. Borrowed AI heroes get their XY velocity and acceleration pinned each frame during the hold so their controller cannot run them back and forth. Post-arm reticle/cursor flicker (<=250 ms) no longer tears down the armed hold. The v57 level-travel GPF fix (live-object validation of every cached pointer + every-frame level-change detector) and the stock P1 SpellCursor.LockOn bridge are retained. Native SpellGesture icon-chain/wet-shader fix, 770-unit aim, and v51 cast gameplay retained."

static FILE *g_log = NULL;
static CRITICAL_SECTION g_logCs;
static BOOL g_logCsInit = FALSE;

static void logf_(const char *fmt, ...)
{
    if (!g_log) return;
    if (g_logCsInit) EnterCriticalSection(&g_logCs);
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log); fflush(g_log);
    if (g_logCsInit) LeaveCriticalSection(&g_logCs);
}

static void loadKeyMap(const char *ini)
{
    for (int i = 1; i < 8; i++) {
        char sect[32]; sprintf(sect, "player%d", i + 1);
        g_pk[i].fwd     = iniKey(sect, "Forward", g_pk[i].fwd,     ini);
        g_pk[i].back    = iniKey(sect, "Back",    g_pk[i].back,    ini);
        g_pk[i].left    = iniKey(sect, "Left",    g_pk[i].left,    ini);
        g_pk[i].right   = iniKey(sect, "Right",   g_pk[i].right,   ini);
        g_pk[i].jump    = iniKey(sect, "Jump",    g_pk[i].jump,    ini);
        g_pk[i].cast    = iniKey(sect, "Cast",    g_pk[i].cast,    ini);
        g_pk[i].use     = iniKey(sect, "Use",     g_pk[i].use,     ini);
        g_pk[i].release = iniKey(sect, "Release", g_pk[i].release, ini);
        g_pk[i].turnL   = iniKey(sect, "TurnLeft",  g_pk[i].turnL,  ini);
        g_pk[i].turnR   = iniKey(sect, "TurnRight", g_pk[i].turnR,  ini);
    }
    logf_("[keys] p2 numpad fwd=0x%02X back=0x%02X left=0x%02X right=0x%02X "
          "jump=0x%02X cast=0x%02X use=0x%02X",
          g_pk[1].fwd, g_pk[1].back, g_pk[1].left, g_pk[1].right,
          g_pk[1].jump, g_pk[1].cast, g_pk[1].use);
    logf_("[keys] p2 alternates: T/G forward-back, F/H TURN (tank), Q/E turn, "
          "R jump, Y cast, V use");
}

// --------------------- input interception (player 2..N) --------------------
// The engine reads the keyboard from the window message queue (WinDrv imports
// PeekMessage/DispatchMessage). Choosing "unbound" keys was not enough: with
// NumLock OFF, Windows reports numpad 8 as VK_UP, which the game binds to
// player 1 -- so pressing player 2's forward key walked Harry.
//
// The numpad and the dedicated arrow cluster are distinguishable: the arrow
// keys set the "extended key" bit (bit 24) of lParam, the numpad does not.
// So we can swallow a numpad-origin VK_UP while leaving the real arrow key
// alone for player 1.
static WNDPROC  g_origWndProc = NULL;
static HWND     g_hookedWnd   = NULL;
static volatile LONG g_numArrow[4] = {0,0,0,0};   // up, down, left, right

static BOOL isPlayerKey(int vk)
{
    for (int i = 1; i < 8; i++) {
        const PlayerKeys &k = g_pk[i];
        if (!k.fwd && !k.fwd2) continue;
        if (vk == k.fwd  || vk == k.back  || vk == k.left  || vk == k.right ||
            vk == k.jump || vk == k.cast  || vk == k.use   || vk == k.release ||
            vk == k.fwd2 || vk == k.back2 || vk == k.left2 || vk == k.right2 ||
            vk == k.jump2|| vk == k.cast2 || vk == k.use2 ||
            vk == k.turnL|| vk == k.turnR)
            return TRUE;
    }
    return FALSE;
}

volatile LONG *numArrowState(void) { return g_numArrow; }

static LRESULT CALLBACK modWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (g_splitOn &&
        (msg == WM_KEYDOWN || msg == WM_KEYUP ||
         msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)) {
        int  vk       = (int)w;
        BOOL extended = (l & (1L << 24)) != 0;
        BOOL down     = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);

        // A numpad key that Windows has folded into an arrow VK because
        // NumLock is off. Claim it for player 2 and hide it from the game.
        if (!extended) {
            int slot = -1;
            if      (vk == VK_UP)    slot = 0;
            else if (vk == VK_DOWN)  slot = 1;
            else if (vk == VK_LEFT)  slot = 2;
            else if (vk == VK_RIGHT) slot = 3;
            if (slot >= 0) {
                InterlockedExchange(&g_numArrow[slot], down ? 1 : 0);
                return 0;                      // swallowed
            }
        }
        if (isPlayerKey(vk)) return 0;         // swallowed
    }
    return CallWindowProcA(g_origWndProc, h, msg, w, l);
}

static BOOL CALLBACK findWndCb(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!IsWindowVisible(h))          return TRUE;
    if (GetWindow(h, GW_OWNER))       return TRUE;   // skip owned popups
    RECT r; GetClientRect(h, &r);
    if ((r.right - r.left) < 200 || (r.bottom - r.top) < 150) return TRUE;
    *(HWND *)lp = h;
    return FALSE;
}

static void hookGameWindow(void)
{
    if (g_origWndProc) return;
    // GetActiveWindow() is per-THREAD: called from the render thread it
    // returns NULL, so the hook silently never installed. Enumerate the
    // process's own top-level windows instead.
    static DWORD lastTry = 0;
    DWORD now = GetTickCount();
    if ((now - lastTry) < 1000) return;
    lastTry = now;

    HWND h = NULL;
    EnumWindows(findWndCb, (LPARAM)&h);
    if (!h) { h = GetActiveWindow(); if (!h) h = GetForegroundWindow(); }
    if (!h) { logf_("[input] no game window found yet - retrying"); return; }

    g_origWndProc = (WNDPROC)SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)modWndProc);
    if (g_origWndProc) {
        g_hookedWnd = h;
        RECT r; GetClientRect(h, &r);
        logf_("[input] SUBCLASSED window %p (%dx%d) - player 2 keys are now "
              "hidden from the engine", h, r.right - r.left, r.bottom - r.top);
    } else {
        logf_("[input] !! SetWindowLongPtr FAILED on %p (err %lu) - player 2 keys "
              "will ALSO reach player 1", h, GetLastError());
    }
}

// ------------------------------ d3d8 proxy ---------------------------------
static HMODULE g_realD3D8 = NULL;
typedef void *(WINAPI *PFN_Direct3DCreate8)(UINT);
static PFN_Direct3DCreate8 g_realCreate = NULL;

// ------------------------------- hooking -----------------------------------
struct Hook { BYTE *target; BYTE *tramp; };

// Resolve an incremental-link thunk (E9 rel32) to the real function body.
static BYTE *resolveThunk(BYTE *p)
{
    for (int i = 0; i < 4 && p && !IsBadReadPtr(p, 5) && p[0] == 0xE9; i++)
        p = p + 5 + *(LONG *)(p + 1);
    return p;
}

// Minimal MSVC-prologue length decoder: accumulate whole instructions until
// we have >= 5 bytes to overwrite with a JMP rel32.
static int prologueLen(const BYTE *p)
{
    int n = 0;
    while (n < 5) {
        BYTE o = p[n];
        int  l;
        if      (o == 0x55 || (o >= 0x50 && o <= 0x57)) l = 1;     // push r32
        else if (o == 0x8B || o == 0x89) {                         // mov r/m,r32
            BYTE m = p[n+1]; int mod = m >> 6, rm = m & 7;
            if      (mod == 3) l = 2;
            else if (mod == 0) l = (rm == 4) ? 3 : ((rm == 5) ? 6 : 2);
            else if (mod == 1) l = (rm == 4) ? 4 : 3;
            else               l = (rm == 4) ? 7 : 6;
        }
        else if (o == 0x6A)                             l = 2;     // push imm8
        else if (o == 0x68)                             l = 5;     // push imm32
        else if (o == 0x83 && p[n+1] == 0xEC)           l = 3;     // sub esp,imm8
        else if (o == 0x81 && p[n+1] == 0xEC)           l = 6;     // sub esp,imm32
        else if (o == 0x90)                             l = 1;     // nop
        else return 0;                                             // unknown
        n += l;
    }
    return n;
}

static BOOL installHook(Hook *h, BYTE *target, void *detour, const char *tag)
{
    if (!target || IsBadReadPtr(target, 16)) { logf_("  [%s] bad target", tag); return FALSE; }
    int n = prologueLen(target);
    logf_("  [%s] target=%p prologue=%02X %02X %02X %02X %02X %02X (len=%d)",
          tag, target, target[0], target[1], target[2], target[3],
          target[4], target[5], n);
    if (n < 5) { logf_("  [%s] ABORT: undecodable prologue, not patching", tag); return FALSE; }

    h->target = target;
    h->tramp  = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                                     PAGE_EXECUTE_READWRITE);
    if (!h->tramp) { logf_("  [%s] ABORT: VirtualAlloc failed", tag); return FALSE; }

    memcpy(h->tramp, target, n);
    h->tramp[n] = 0xE9;
    *(LONG *)(h->tramp + n + 1) = (LONG)(target + n) - (LONG)(h->tramp + n + 5);

    DWORD old;
    if (!VirtualProtect(target, n, PAGE_EXECUTE_READWRITE, &old)) {
        logf_("  [%s] ABORT: VirtualProtect failed", tag); return FALSE;
    }
    memset(target, 0x90, n);
    target[0] = 0xE9;
    *(LONG *)(target + 1) = (LONG)detour - (LONG)(target + 5);
    VirtualProtect(target, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, n);
    logf_("  [%s] HOOKED, trampoline=%p", tag, h->tramp);
    return TRUE;
}

// --------------------------- engine symbols --------------------------------
#define SYM_DRAW      "?Draw@UGameEngine@@UAEXPAVUViewport@@HPAEPAH@Z"
#define SYM_POSTREND  "?MasterProcessPostRender@UInteractionMaster@@QAEXPAVUCanvas@@@Z"
#define SYM_PSNODE    "??0FPlayerSceneNode@@QAE@PAVUViewport@@PAVFRenderTarget@@PAVAActor@@VFVector@@VFRotator@@M@Z"
#define SYM_DRAWPORT  "?execDrawPortal@UCanvas@@QAEXAAUFFrame@@QAX@Z"
#define SYM_GNATIVES  "?GNatives@@3PAP8UObject@@AEXAAUFFrame@@QAX@ZA"

static HMODULE g_core = NULL, g_engine = NULL;

// --------------------- UnrealScript opcode discovery -----------------------
struct OpDef { const char *sym; const char *name; int op; };
static OpDef g_ops[] = {
    { "?execIntConst@UObject@@QAEXAAUFFrame@@QAX@Z",      "IntConst",        -1 },
    { "?execObjectConst@UObject@@QAEXAAUFFrame@@QAX@Z",   "ObjectConst",     -1 },
    { "?execVectorConst@UObject@@QAEXAAUFFrame@@QAX@Z",   "VectorConst",     -1 },
    { "?execRotationConst@UObject@@QAEXAAUFFrame@@QAX@Z", "RotationConst",   -1 },
    { "?execTrue@UObject@@QAEXAAUFFrame@@QAX@Z",          "True",            -1 },
    { "?execFalse@UObject@@QAEXAAUFFrame@@QAX@Z",         "False",           -1 },
    { "?execEndFunctionParms@UObject@@QAEXAAUFFrame@@QAX@Z","EndFunctionParms",-1 },
};
enum { OP_INT = 0, OP_OBJ, OP_VEC, OP_ROT, OP_TRUE, OP_FALSE, OP_END, OP_COUNT };
static int g_opNameConst = -1;   // optional execNameConst opcode (Spawn tag)
static int g_opByteConst = -1;   // optional: SetPhysics takes a BYTE, not an int

static BOOL discoverOpcodes(void)
{
    void **tbl = (void **)GetProcAddress(g_core, SYM_GNATIVES);
    logf_("GNatives table = %p", (void *)tbl);
    if (!tbl || IsBadReadPtr(tbl, 256 * 4)) { logf_("  GNatives unreadable"); return FALSE; }

    BOOL all = TRUE;
    for (int i = 0; i < OP_COUNT; i++) {
        BYTE *want = resolveThunk((BYTE *)GetProcAddress(g_core, g_ops[i].sym));
        if (!want) { logf_("  %-17s EXPORT MISSING", g_ops[i].name); all = FALSE; continue; }
        for (int op = 0; op < 256; op++) {
            if (resolveThunk((BYTE *)tbl[op]) == want) { g_ops[i].op = op; break; }
        }
        if (g_ops[i].op < 0) { logf_("  %-17s NOT FOUND in GNatives (%p)", g_ops[i].name, want); all = FALSE; }
        else logf_("  %-17s = opcode 0x%02X   (handler %p)", g_ops[i].name, g_ops[i].op, want);
    }
    // Optional token, NOT gating g_opsOK: NameConst feeds name parms (the
    // Spawn tag). Without it the aim glow disables itself; everything else
    // keeps working.
    g_opNameConst = -1;
    BYTE *nc = resolveThunk((BYTE *)GetProcAddress(g_core,
                 "?execNameConst@UObject@@QAEXAAUFFrame@@QAX@Z"));
    if (nc) {
        for (int op = 0; op < 256; op++)
            if (resolveThunk((BYTE *)tbl[op]) == nc) { g_opNameConst = op; break; }
    }
    logf_("  %-17s = %s", "NameConst(opt)",
          g_opNameConst >= 0 ? "opcode found" : "NOT FOUND (aim glow off)");
    // Optional as well: unavailable floor repair must not disable rendering.
    // IntConst writes four bytes; using it for P_GET_BYTE would corrupt the
    // native's parameter storage. Discover the correctly sized token instead.
    g_opByteConst = -1;
    BYTE *bc = resolveThunk((BYTE *)GetProcAddress(g_core,
                 "?execByteConst@UObject@@QAEXAAUFFrame@@QAX@Z"));
    if (bc) {
        for (int op = 0; op < 256; op++)
            if (resolveThunk((BYTE *)tbl[op]) == bc) { g_opByteConst = op; break; }
    }
    logf_("  %-17s = %s", "ByteConst(opt)",
          g_opByteConst >= 0 ? "opcode found" : "NOT FOUND (floor repair off)");
    return all;
}

// --------------------- object enumeration / naming -------------------------
// Core.dll exports the global object table and the name helpers, so we can
// walk every live UObject and print "Class Package.Name" without knowing the
// UObject::Class field offset.
#define SYM_GOBJ     "?GObjObjects@UObject@@0V?$TArray@PAVUObject@@@@A"
#define SYM_FULLNAME "?GetFullName@UObject@@QBEPBGPAG@Z"
#define SYM_GETNAME  "?GetName@UObject@@QBEPBGXZ"

struct TArrayLite { void **Data; int Num; int Max; };

typedef const wchar_t *(__fastcall *PFN_GetFullName)(void *self, void *edx, wchar_t *buf);
typedef const wchar_t *(__fastcall *PFN_GetName)(void *self, void *edx);

static TArrayLite      *g_objArray = NULL;
static PFN_GetFullName  g_GetFullName = NULL;
static PFN_GetName      g_GetName = NULL;

// Copy a UObject's "Class Package.Name" into an ASCII buffer.
static const char *objName(void *o, char *out, int cch)
{
    out[0] = 0;
    if (!o || !g_GetFullName || IsBadReadPtr(o, 0x30)) { strcpy(out, "<null>"); return out; }
    const wchar_t *w = g_GetFullName(o, NULL, NULL);
    if (!w || IsBadReadPtr((void *)w, 2)) { strcpy(out, "<noname>"); return out; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cch, NULL, NULL);
    out[cch - 1] = 0;
    return out;
}

// Just the object's own name ("ProcessTouch"), no outer chain: far cheaper
// than GetFullName, for hot-path filters.
static const char *objShortName(void *o, char *out, int cch)
{
    out[0] = 0;
    if (!o || !g_GetName || IsBadReadPtr(o, 0x30)) return out;
    const wchar_t *w = g_GetName(o, NULL);
    if (!w || IsBadReadPtr((void *)w, 2)) return out;
    int k = 0;
    for (; k < cch - 1; k++) {
        if (k && (((ULONG_PTR)(w + k)) & 0xFFF) == 0 && IsBadReadPtr((void *)(w + k), 2)) break;
        if (!w[k]) break;
        if (w[k] < 0x20 || w[k] > 0x7E) { out[0] = 0; return out; }   // not a name
        out[k] = (char)w[k];
    }
    out[k] = 0;
    return out;
}

// v57 crash fix: a cached actor pointer can outlive the level that owned it.
// IsBadReadPtr only proves the page is still READABLE - UObject memory that
// was freed by a level change very often still is, and every IsBadReadPtr-
// guarded dereference (objName -> UObject::GetFullName walks the object's
// Outer chain; field reads; Destroy on a cached FX) then runs on freed
// memory that the new level's allocations have started to reuse. Hardware
// crash entering Hogwarts as Harry, split off:
//   UObject::GetPathName <- UObject::GetPathName <- UObject::GetFullName
//   <- FPlayerSceneNode::Render <- UGameEngine::Draw
// (the mod's PostRender detour frame has no frame pointer, so the walker
// attributes the call to FPlayerSceneNode::Render, the engine function that
// invokes UInteractionMaster::MasterProcessPostRender).
// GObjObjects is the engine's own liveness record: only live UObjects appear
// in it, and everything here runs on the game thread, so table membership is
// an exact is-this-object-still-alive answer. Used on CACHED pointers and on
// object references read out of live actors; freshly table-scanned objects
// are live by construction and skip this check.
static BOOL cgLiveObject(void *o)
{
    if (!o || !g_objArray) return FALSE;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000) return FALSE;
    if (IsBadReadPtr(g_objArray->Data, 4 * (DWORD)n)) return FALSE;
    void **data = g_objArray->Data;
    for (int i = 0; i < n; i++)
        if (data[i] == o) return TRUE;
    return FALSE;
}

// True for UnrealScript reflection/metadata objects we never care about.
static BOOL isReflection(const char *full)
{
    char cls[64]; int i = 0;
    while (full[i] && full[i] != ' ' && i < 63) { cls[i] = full[i]; i++; }
    cls[i] = 0;
    int L = (int)strlen(cls);
    if (L > 8 && !strcmp(cls + L - 8, "Property")) return TRUE;
    static const char *skip[] = {
        "Function", "State", "Class", "Enum", "ScriptStruct", "Struct", "Const",
        "TextBuffer", "Package", "Field", "Texture", "Sound", "Palette", "Font",
        "StaticMesh", "Mesh", "LodMesh", "SkeletalMesh", "Animation", "Shader",
        "Material", "Combiner", "FinalBlend", "ConstantColor", "TexPanner",
        "Model", "Polys", "Bitmap", "Modifier", "TexScaler", "TexRotator",
        "MeshAnimation", "Cubemap", "TexOscillator", "ColorModifier", NULL
    };
    for (int k = 0; skip[k]; k++) if (!strcmp(cls, skip[k])) return TRUE;
    return FALSE;
}

// Dump every live object whose full name matches one of the filters.
static void dumpObjects(const char *const *filters)
{
    if (!g_objArray || IsBadReadPtr(g_objArray, sizeof(TArrayLite))) {
        logf_("[objdump] GObjObjects unavailable"); return;
    }
    int n = g_objArray->Num;
    logf_("[objdump] GObjObjects: %d slots @ %p", n, (void *)g_objArray->Data);
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return;

    char buf[512];
    int shown = 0, live = 0;
    for (int i = 0; i < n && shown < 1500; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        live++;
        objName(o, buf, sizeof(buf));
        if (isReflection(buf)) continue;
        for (int f = 0; filters[f]; f++) {
            if (strstr(buf, filters[f])) {
                logf_("  [%6d] %p  %s", i, o, buf);
                shown++;
                break;
            }
        }
    }
    logf_("[objdump] %d live objects scanned, %d matched", live, shown);
}

// --------------------- per-player character binding ------------------------
// Each split view follows a different character. View 0 keeps the engine's own
// polished camera (HPCam); views 1..N-1 get a third-person camera anchored to
// their own pawn. Class tokens come from the live object dump:
//     harry HP3_Adv1Express.Harry0
//     Hermione HP3_Adv1Express.Hermione2
//     Ron HP3_Adv1Express.Ron1
static const char *kCharClass[8] = {
    "harry", "Hermione", "Ron", "harry", "Hermione", "Ron", "harry", "Hermione"
};
static void *g_pawn[8]     = {0};
static char  g_pawnName[8][96];

static void *g_camActor = NULL;
static BOOL cgDeleted(void *o);
// Only actors in the camera's current map qualify. Old HP_preamble actors
// can remain in GObjObjects after travel without bDeleteMe set.
static BOOL actorInCurrentLevel(void *o) {
    if (!o || !g_camActor || IsBadReadPtr(o, 0x2C) || IsBadReadPtr(g_camActor, 0x2C)) return FALSE;
    // v57 crash fix: g_camActor itself can be a stale pointer from the level
    // that was just destroyed (frames without a fresh FPlayerSceneNode, or a
    // mid-frame level change). Never hand a dead camera to GetFullName; the
    // liveness result is memoised per camera pointer because this helper is
    // on the candidate-scan hot path (thousands of calls per scan).
    static void *sCam = NULL; static DWORD sCamAt = 0; static BOOL sCamOK = FALSE;
    DWORD nowCam = GetTickCount();
    if (g_camActor != sCam || !sCamOK || !sCamAt ||
        (DWORD)(nowCam - sCamAt) > 250u) {
        sCam = g_camActor; sCamAt = nowCam; sCamOK = cgLiveObject(g_camActor);
    }
    if (!sCamOK) return FALSE;
    char a[256], c[256]; objName(o,a,sizeof(a)); objName(g_camActor,c,sizeof(c));
    return hp3aim::samePackage(a,c);
}
static float g_aimViewLoc[8][3];
static int g_aimViewRot[8][3];
static BOOL g_aimViewValid[8] = {0};
static float nativeAimRange(void);

// Linear scan for a current-level actor, never a stale preamble instance.
static void *findActorByClass(const char *cls)
{
    if (!g_objArray || IsBadReadPtr(g_objArray, sizeof(TArrayLite))) return NULL;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return NULL;
    int L = (int)strlen(cls);
    char buf[256];
    for (int i = 0; i < n; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        objName(o, buf, sizeof(buf));
        if (!strncmp(buf, cls, L) && buf[L] == ' ' && actorInCurrentLevel(o) && !cgDeleted(o)) return o;
    }
    return NULL;
}

// Resolve (and cache) the pawn backing split view `i`.
static void *getPawn(int i)
{
    if (i < 0 || i > 7) return NULL;
    // v57: g_pawn[] is cached across frames - a level change frees the old
    // pawns while the cache still points at them. Never name or touch a pawn
    // that is no longer a live UObject (see cgLiveObject).
    if (g_pawn[i] && !cgLiveObject(g_pawn[i])) g_pawn[i] = NULL;
    if (g_pawn[i] && actorInCurrentLevel(g_pawn[i]) && !cgDeleted(g_pawn[i])) {
        char name[160]; objName(g_pawn[i],name,sizeof(name));
        size_t len=strlen(kCharClass[i]);
        if(!strncmp(name,kCharClass[i],len) && name[len]==' ') return g_pawn[i];
    }
    g_pawn[i] = NULL;
    void *o = findActorByClass(kCharClass[i]);
    if (o) {
        g_pawn[i] = o;
        objName(o, g_pawnName[i], sizeof(g_pawnName[i]));
        float *pl = (float *)((BYTE *)o + 0x150);
        logf_("  player %d -> %s @ %p  loc=(%.0f %.0f %.0f)",
              i, g_pawnName[i], o, pl[0], pl[1], pl[2]);
    }
    return o;
}

// Third-person camera behind a pawn, in UE rotator units (65536 = 360 deg).
static BOOL g_opsOK = FALSE;

struct FFrameLite {
    void  *vtable;   // FOutputDevice
    void  *Node;
    void  *Object;
    BYTE  *Code;
    BYTE  *Locals;
    DWORD  pad[12];
};

typedef void (__fastcall *PFN_execDrawPortal)(void *canvas, void *edx,
                                              FFrameLite *stack, void *result);
static PFN_execDrawPortal g_execDrawPortal = NULL;

// Engine.Actor.FastTrace is a NATIVE. Natives read their arguments from the
// bytecode stream, not from the ProcessEvent parms block, which is why calling
// Engine.Actor.Trace through ProcessEvent silently returned nothing. Natives
// must be driven the same way DrawPortal is: with a synthesized instruction
// stream. FastTrace is ideal for this because it has no out-parameters --
// only two vectors in and a bool out.
typedef void (__fastcall *PFN_execFastTrace)(void *actor, void *edx,
                                             FFrameLite *stack, void *result);
static PFN_execFastTrace g_execFastTrace = NULL;

// TRUE when the straight line start->end is unobstructed by world geometry.
static BOOL fastTraceClear(void *actor, const float start[3], const float end[3])
{
    if (!g_execFastTrace || !g_opsOK || !actor) return TRUE;
    BYTE bc[32]; int p = 0;
    bc[p++] = (BYTE)g_ops[OP_VEC].op;
    *(float *)(bc+p)=end[0];   p+=4; *(float *)(bc+p)=end[1];   p+=4; *(float *)(bc+p)=end[2];   p+=4;
    bc[p++] = (BYTE)g_ops[OP_VEC].op;
    *(float *)(bc+p)=start[0]; p+=4; *(float *)(bc+p)=start[1]; p+=4; *(float *)(bc+p)=start[2]; p+=4;
    bc[p++] = (BYTE)g_ops[OP_END].op;
    bc[p++] = (BYTE)g_ops[OP_END].op;      // guard

    FFrameLite st; memset(&st, 0, sizeof(st));
    st.Object = actor; st.Code = bc;
    DWORD result = 0;
    g_execFastTrace(actor, NULL, &st, &result);
    return result != 0;
}

typedef void (__fastcall *PFN_ProcessEvent)(void *self, void *edx,
                                            void *func, void *parms, void *result);
static PFN_ProcessEvent g_ProcessEvent = NULL;

// Engine.Actor.Spawn / Engine.Actor.Destroy, driven the same way FastTrace is:
// natives read their parms as bytecode TOKENS from the FFrame, one const per
// parm in declaration order. Calling them through ProcessEvent handed them a
// parms block they never read - Spawn silently returned NULL every frame.
// Spawn layout (runtime-dumped): class@0 owner@4 tag(Name,4B)@0x08 loc@0x0C
// rot@0x18 ret@0x24. Tokens: OBJ, OBJ, NAME(index DWORD), VEC(12), ROT(12).
typedef void (__fastcall *PFN_execActorFn)(void *actor, void *edx,
                                           FFrameLite *stack, void *result);
static PFN_execActorFn g_execSpawn = NULL, g_execDestroy = NULL;

// v49: do not call native SetPhysics through ProcessEvent. Like Spawn and
// FastTrace, it consumes FFrame bytecode. The native initializes walking's
// Base/Floor bookkeeping; writing the Physics byte bypasses that work.
static PFN_execActorFn g_execSetPhysics = NULL;
typedef void (__fastcall *PFN_FindBase)(void *actor, void *edx);
static PFN_FindBase g_FindBase = NULL;

static BOOL nativeSetPhysics(void *pawn, BYTE physics)
{
    if (!pawn || !g_opsOK || g_opByteConst < 0 || !g_execSetPhysics) return FALSE;
    BYTE bc[] = { (BYTE)g_opByteConst, physics,
                  (BYTE)g_ops[OP_END].op, (BYTE)g_ops[OP_END].op };
    FFrameLite st; memset(&st, 0, sizeof(st));
    st.Object = pawn; st.Code = bc;
    DWORD result = 0;
    g_execSetPhysics(pawn, NULL, &st, &result);
    return TRUE; // invocation succeeded; the caller must VERIFY the contact
}

static void *spawnFX(void *actor, void *cls, void *owner,
                     const float loc[3], const int rot[3])
{
    if (!g_execSpawn || !g_opsOK || g_opNameConst < 0 || !actor || !cls)
        return NULL;
    BYTE bc[64]; int p = 0;
    bc[p++] = (BYTE)g_ops[OP_OBJ].op;  *(void **)(bc + p) = cls;   p += 4;
    bc[p++] = (BYTE)g_ops[OP_OBJ].op;  *(void **)(bc + p) = owner; p += 4;
    bc[p++] = (BYTE)g_opNameConst;     *(DWORD *)(bc + p) = 0;     p += 4; // NAME_None
    bc[p++] = (BYTE)g_ops[OP_VEC].op;  memcpy(bc + p, loc, 12);    p += 12;
    bc[p++] = (BYTE)g_ops[OP_ROT].op;  memcpy(bc + p, rot, 12);    p += 12;
    bc[p++] = (BYTE)g_ops[OP_END].op;
    bc[p++] = (BYTE)g_ops[OP_END].op;  // guard
    FFrameLite st; memset(&st, 0, sizeof(st));
    st.Object = actor; st.Code = bc;
    void *result = NULL;
    g_execSpawn(actor, NULL, &st, &result);
    return result;
}

// Engine.Actor.Destroy has no parms - the frame only needs the END guard.
// Returns Destroy()'s bool (FALSE when the actor refused to die).
static BOOL destroyActorFX(void *fx)
{
    if (!g_execDestroy || !g_opsOK || !fx) return FALSE;
    BYTE bc[4]; int p = 0;
    bc[p++] = (BYTE)g_ops[OP_END].op;
    bc[p++] = (BYTE)g_ops[OP_END].op;   // guard
    FFrameLite st; memset(&st, 0, sizeof(st));
    st.Object = fx; st.Code = bc;
    DWORD result = 0;
    g_execDestroy(fx, NULL, &st, &result);
    return result != 0;
}

// Trailing camera for a driven pawn. The desired eye position is probed with
// a series of FastTrace line checks and the camera settles at the furthest
// unobstructed distance, so it slides in against walls instead of punching
// through them in tight corridors.
static float g_camDistCur[8] = {0};

int playerViewYaw(int i);
int playerCamYaw(int i);
int playerCamPitch(int i);
void resetViewYaw(void);
static void thirdPersonCam(int idx, void *pawn, float outLoc[3], int outRot[3])
{
    const float DIST = g_camDist, HEIGHT = g_camHeight;
    float *pl = (float *)((BYTE *)pawn + 0x150);

    // Camera yaw comes from the player's view yaw, not the pawn's rotation.
    int camYaw = playerCamYaw(idx);
    int pitch  = playerCamPitch(idx);
    outRot[0] = pitch; outRot[1] = camYaw; outRot[2] = 0;

    // v14: the camera ORBITS the focus point by yaw AND pitch, like the
    // original player's camera. v13 held the eye at a fixed height and only
    // rotated the view, so pitching down just stared at the character's feet.
    // Now pitching down swings the camera up overhead with the character
    // centered in frame; pitching up drops the camera low behind.
    float pivot[3] = { pl[0], pl[1], pl[2] + HEIGHT };
    double ry = camYaw * (6.283185307179586 / 65536.0);
    double rp = pitch  * (6.283185307179586 / 65536.0);
    float cosp = (float)cos(rp);
    // unit view direction (where the camera looks); the eye sits behind it
    float vx = (float)cos(ry) * cosp, vy = (float)sin(ry) * cosp, vz = (float)sin(rp);

    static const float frac[] = { 1.00f, 0.80f, 0.62f, 0.46f, 0.32f, 0.18f };
    const int NFRAC = (int)(sizeof(frac) / sizeof(frac[0]));
    float want = DIST;

    if (g_camCollide && g_execFastTrace) {
        // Never collapse the camera into the character's head: if even the
        // closest probe is blocked, accept the closest one and let it clip a
        // little rather than putting the eye inside the pawn.
        want = DIST * frac[NFRAC - 1];
        for (int k = 0; k < NFRAC; k++) {
            float d = DIST * frac[k];
            float cand[3] = { pivot[0] - vx * d, pivot[1] - vy * d,
                              pivot[2] - vz * d };
            if (fastTraceClear(pawn, pivot, cand)) { want = d; break; }
        }
    }

    // Temporal smoothing: snap inwards so walls never clip through, but ease
    // back out, or the view jitters as the probe result flips frame to frame.
    if (idx < 0 || idx >= 8) idx = 0;
    float cur = g_camDistCur[idx];
    if (cur <= 0.0f)      cur = want;
    else if (want < cur)  cur = want;                       // pull in at once
    else                  cur += (want - cur) * 0.10f;      // ease back out (less shake)
    g_camDistCur[idx] = cur;

    static int camLog = 0;
    if (cur < DIST - 0.5f && (camLog++ % 12) == 0)
        logf_("  [cam] p%d obstructed - %.0f -> %.0f units (target %.0f)",
              idx, DIST, cur, want);

    // When the camera cannot get behind the character at all, sitting at the
    // pivot would put the eye inside its head and the character would vanish
    // from its own view. Instead, lift the camera and steepen the pitch so it
    // looks down over the shoulder - the view stays usable in tight corners.
    // (v15: steepen only while looking level/down; steepening while the player
    // holds look-UP would yank the view back down mid-aim.)
    float t = cur / (DIST > 1.0f ? DIST : 1.0f);        // 1 = fully extended
    float lift = 0.0f;
    if (t < 0.55f) {
        float k = (0.55f - t) / 0.55f;                  // 0 at t=0.55, 1 at t=0
        lift  = k * 35.0f;
        if (pitch <= 0) {
            pitch += (int)(k * -5000.0f);
            if (pitch < -16000) pitch = -16000;
            outRot[0] = pitch;
        }
    }
    outLoc[0] = pivot[0] - vx * cur;
    outLoc[1] = pivot[1] - vy * cur;
    outLoc[2] = pivot[2] - vz * cur + lift;
}


// Find an object by its path ("Engine.Actor.Location"), ignoring the leading
// class token that GetFullName prepends.
static void *findObjectByPath(const char *path)
{
    if (!g_objArray || IsBadReadPtr(g_objArray, sizeof(TArrayLite))) return NULL;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return NULL;
    char buf[256];
    for (int i = 0; i < n; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        objName(o, buf, sizeof(buf));
        const char *sp = strchr(buf, ' ');
        if (sp && !strcmp(sp + 1, path)) return o;
    }
    return NULL;
}

// Locate UProperty::Offset once, using fields whose offsets we already know
// for certain (Actor.Location = 0x150, Actor.Rotation = 0x15C). After this,
// ANY field offset can be looked up by name instead of guessed.
static int g_propOffsetField = -1;

static void calibratePropertyOffset(void)
{
    struct { const char *path; DWORD known; } probe[] = {
        { "Engine.Actor.Location", 0x150 },
        { "Engine.Actor.Rotation", 0x15C },
    };
    int cand[2][32]; int ncand[2] = {0, 0};

    for (int k = 0; k < 2; k++) {
        void *p = findObjectByPath(probe[k].path);
        logf_("  %s -> %p", probe[k].path, p);
        if (!p || IsBadReadPtr(p, 0x80)) continue;
        for (int off = 0; off + 4 <= 0x80; off += 4)
            if (*(DWORD *)((BYTE *)p + off) == probe[k].known && ncand[k] < 32)
                cand[k][ncand[k]++] = off;
        char line[256]; int c = sprintf(line, "    dwords==0x%lX at:", probe[k].known);
        for (int j = 0; j < ncand[k]; j++) c += sprintf(line + c, " +0x%X", cand[k][j]);
        logf_("%s", line);
    }
    // The one offset that works for BOTH probes is UProperty::Offset.
    for (int a = 0; a < ncand[0]; a++)
        for (int b = 0; b < ncand[1]; b++)
            if (cand[0][a] == cand[1][b]) { g_propOffsetField = cand[0][a]; }
    logf_("  ==> UProperty::Offset field = +0x%X", g_propOffsetField);
}

// Resolve a script field's byte offset within its object, by name.
static int propOffset(const char *path)
{
    if (g_propOffsetField < 0) return -1;
    void *p = findObjectByPath(path);
    if (!p || IsBadReadPtr(p, g_propOffsetField + 4)) return -1;
    return (int)*(DWORD *)((BYTE *)p + g_propOffsetField);
}

// List the script functions available on the hero pawn classes, so player 2's
// actions can be driven through UObject::ProcessEvent instead of poking fields.
static void dumpHeroFunctions(void)
{
    if (!g_objArray || IsBadReadPtr(g_objArray, sizeof(TArrayLite))) return;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return;

    static const char *want[] = {
        ".harry.", ".Hermione.", ".Ron.", ".HPHeroPawn.", ".HPCharacter.",
        ".HPPawn.", ".KWPawn.", "Engine.Pawn.",
        ".HarryController.", ".HPHeroController.", ".KWHeroController.",
        ".HPCompanionController.", ".HPAIController.", NULL
    };
    char buf[300];
    int shown = 0;
    logf_("--- hero pawn script functions ---");
    for (int i = 0; i < n && shown < 900; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        objName(o, buf, sizeof(buf));
        if (strncmp(buf, "Function ", 9)) continue;
        // only top-level functions (Package.Class.Func), not their locals
        int dots = 0;
        for (const char *c = buf; *c; c++) if (*c == '.') dots++;
        if (dots != 2) continue;
        for (int w = 0; want[w]; w++) {
            if (strstr(buf, want[w])) { logf_("    %p  %s", o, buf); shown++; break; }
        }
    }
    logf_("--- %d hero functions ---", shown);
}

// Dump one script function's real signature. HGame.u is source-stripped, but
// a UFunction's parameters and locals are themselves objects in GObjObjects,
// named "Pkg.Class.Func.Param". Their class token gives the type and the
// dword at UProperty::Offset gives their position in the parms block, so a
// call frame can be built exactly without any source.
static void dumpFunctionSig(const char *path)
{
    if (!g_objArray || g_propOffsetField < 0) return;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000) return;
    size_t plen = strlen(path);
    char buf[300];
    void *fn = findObjectByPath(path);
    logf_("  fn %s @ %p", path, fn);
    if (!fn) return;

    struct { int off; char txt[160]; } ent[48]; int ne = 0;
    for (int i = 0; i < n && ne < 48; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        objName(o, buf, sizeof(buf));
        const char *sp = strchr(buf, ' ');
        if (!sp) continue;
        const char *full = sp + 1;
        if (strncmp(full, path, plen) || full[plen] != '.') continue;
        if (strchr(full + plen + 1, '.')) continue;         // direct children only
        if (IsBadReadPtr(o, g_propOffsetField + 4)) continue;
        int off = (int)*(DWORD *)((BYTE *)o + g_propOffsetField);
        *(char *)sp = 0;                                     // split class token
        ent[ne].off = off;
        _snprintf(ent[ne].txt, sizeof(ent[ne].txt) - 1, "+0x%02X  %-16s %s",
                  off, buf, full + plen + 1);
        ent[ne].txt[sizeof(ent[ne].txt) - 1] = 0;
        ne++;
    }
    for (int a = 0; a < ne; a++)                             // sort by frame offset
        for (int b = a + 1; b < ne; b++)
            if (ent[b].off < ent[a].off) { char t[160]; int x = ent[a].off;
                ent[a].off = ent[b].off; ent[b].off = x;
                memcpy(t, ent[a].txt, 160); memcpy(ent[a].txt, ent[b].txt, 160);
                memcpy(ent[b].txt, t, 160); }
    if (!ne) logf_("      (no params -- void call)");
    for (int a = 0; a < ne; a++) logf_("      %s", ent[a].txt);
}

// Every property whose name contains a keyword, on the hero classes: this is
// how the "which spell is selected" state field gets found.
static void dumpPropsMatching(const char *keyword)
{
    if (!g_objArray || g_propOffsetField < 0) return;
    int n = g_objArray->Num; if (n <= 0 || n > 400000) return;
    char buf[300]; int shown = 0;
    logf_("--- properties matching '%s' ---", keyword);
    for (int i = 0; i < n && shown < 120; i++) {
        void *o = g_objArray->Data[i]; if (!o) continue;
        objName(o, buf, sizeof(buf));
        if (!strstr(buf, "Property ")) continue;
        int dots = 0; for (const char *c = buf; *c; c++) if (*c == '.') dots++;
        if (dots != 2) continue;                             // Pkg.Class.Field
        if (!strstr(buf, keyword)) continue;
        if (!(strstr(buf, ".HPCharacter.") || strstr(buf, ".HPHeroPawn.") ||
              strstr(buf, ".HPPawn.")      || strstr(buf, ".KWPawn.")     ||
              strstr(buf, ".HPHeroController.") || strstr(buf, ".HPAIController.")))
            continue;
        if (IsBadReadPtr(o, g_propOffsetField + 4)) continue;
        logf_("    +0x%03X  %s", (int)*(DWORD *)((BYTE *)o + g_propOffsetField), buf);
        shown++;
    }
    logf_("--- %d matches ---", shown);
}

// Enumerate loaded Classes whose name contains a fragment. castSpell takes a
// ClassProperty, so an actual spell class pointer is required.
static void dumpClassesMatching(const char *frag)
{
    if (!g_objArray) return;
    int n = g_objArray->Num; if (n <= 0 || n > 400000) return;
    char buf[300]; int shown = 0;
    logf_("--- Class objects containing '%s' ---", frag);
    for (int i = 0; i < n && shown < 200; i++) {
        void *o = g_objArray->Data[i]; if (!o) continue;
        objName(o, buf, sizeof(buf));
        if (strncmp(buf, "Class ", 6)) continue;
        if (!strstr(buf, frag)) continue;
        logf_("    %p  %s", o, buf); shown++;
    }
    logf_("--- %d spell classes ---", shown);
}

// ALL direct properties of one exact class (any package), sorted by offset.
static void dumpClassPropsFull(const char *classPath)
{
    if (!g_objArray || g_propOffsetField < 0) return;
    int n = g_objArray->Num; if (n <= 0 || n > 400000) return;
    size_t plen = strlen(classPath);
    char buf[300];
    struct { int off; char txt[200]; } ent[128]; int ne = 0;
    logf_("--- all props of %s ---", classPath);
    for (int i = 0; i < n && ne < 128; i++) {
        void *o = g_objArray->Data[i]; if (!o) continue;
        objName(o, buf, sizeof(buf));
        const char *sp = strchr(buf, ' '); if (!sp) continue;
        if (!strstr(buf, "Property ")) continue;         // any *Property type
        const char *full = sp + 1;
        if (strncmp(full, classPath, plen) || full[plen] != '.') continue;
        if (strchr(full + plen + 1, '.')) continue;       // direct children only
        if (IsBadReadPtr(o, g_propOffsetField + 4)) continue;
        int off = (int)*(DWORD *)((BYTE *)o + g_propOffsetField);
        _snprintf(ent[ne].txt, sizeof(ent[ne].txt) - 1, "+0x%03X  %-14s %s",
                  off, buf, full + plen + 1);   // buf = "ObjectProperty" etc
        ent[ne].txt[sizeof(ent[ne].txt) - 1] = 0;
        ent[ne].off = off; ne++;
    }
    for (int a = 0; a < ne; a++)
        for (int b = a + 1; b < ne; b++)
            if (ent[b].off < ent[a].off) {
                int x = ent[a].off; ent[a].off = ent[b].off; ent[b].off = x;
                char t[200]; memcpy(t, ent[a].txt, 200);
                memcpy(ent[a].txt, ent[b].txt, 200); memcpy(ent[b].txt, t, 200);
            }
    if (!ne) logf_("    (no properties found)");
    for (int a = 0; a < ne; a++) logf_("    %s", ent[a].txt);
}

// Properties matching a keyword on ANY class (dumpPropsMatching is hero-only).
static void dumpPropsAny(const char *keyword)
{
    if (!g_objArray || g_propOffsetField < 0) return;
    int n = g_objArray->Num; if (n <= 0 || n > 400000) return;
    char buf[300]; int shown = 0;
    logf_("--- any-class props matching '%s' ---", keyword);
    for (int i = 0; i < n && shown < 120; i++) {
        void *o = g_objArray->Data[i]; if (!o) continue;
        objName(o, buf, sizeof(buf));
        if (!strstr(buf, "Property ")) continue;
        if (!strstr(buf, keyword)) continue;
        if (IsBadReadPtr(o, g_propOffsetField + 4)) continue;
        logf_("    +0x%03X  %s", (int)*(DWORD *)((BYTE *)o + g_propOffsetField), buf);
        shown++;
    }
    logf_("--- %d matches ---", shown);
}

// The signatures player 2 needs: casting, pickup/use, and AI disown.
static void dumpActionSigs(void)
{
    static const char *sigs[] = {
        "hgame.HPCharacter.castSpell",     "hgame.HPCharacter.ChooseSpell",
        "hgame.HPCharacter.HasSpell",      "hgame.HPCharacter.canCast",
        "hgame.HPCharacter.StartCasting",  "hgame.HPCharacter.StopCasting",
        "hgame.HPCharacter.finalizeSpell", "hgame.HPCharacter.playCast",
        "hgame.HPCharacter.Fire",          "hgame.HPPawn.SpawnSpell",
        "hgame.HPPawn.SpawnSpellEx",       "hgame.HPPawn.HandleSpell",
        "hgame.HPHeroController.Fire",     "hgame.HPHeroPawn.canCast",
        "KWGame.KWPawn.PickupActor",       "KWGame.KWPawn.CanDoPickupActor",
        "KWGame.KWPawn.ObjectPickup",      "Engine.Pawn.HandlePickup",
        "Engine.Controller.UnPossess",     "Engine.Controller.Possess",
        "Engine.Pawn.SetAnimAction",       "KWGame.KWPawn.Trigger",
        "Engine.Pawn.Trigger",             "KWGame.KWHeroController.SwitchControlToPawn",
        "KWGame.KWHeroController.SwitchToPawn", "KWGame.KWHeroController.SwapPawn",
        "KWGame.KWHeroController.UnPossess","KWGame.KWPawn.CanDoPickupActor",
        "hgame.HPCharacter.IsAimingOrCasting",
        "Engine.Actor.Trace",              "Engine.Actor.FastTrace",
        "Engine.Actor.TraceActors",        "hgame.HPCharacter.playCastAim",
        "hgame.HPCharacter.CanCastAim",    "hgame.HPCharacter.SetupCastingAnimation",
        "KWGame.KWPawn.SetTargetLocations","Engine.Actor.SetRotation",
        "hgame.HPCharacter.GetSpellSound", "hgame.HPPawn.OnHandleSpell",
        "Engine.Actor.Spawn",              "Engine.Actor.Destroy",
        NULL
    };
    logf_("--- action signatures ---");
    for (int i = 0; sigs[i]; i++) dumpFunctionSig(sigs[i]);
    dumpClassesMatching("pell");
    dumpPropsMatching("Spell");
    dumpPropsMatching("Target");
    dumpPropsMatching("Collision");
    dumpPropsMatching("spell");
    logf_("--- end signatures ---");
}

// ---- player 2..N actions, driven through the engine's own script VM --------
// Movement is a field override, but real actions (jump, spellcast) must run
// the pawn's UnrealScript so animation/sound/state all stay consistent.

static void *g_fnJump = NULL, *g_fnFire = NULL, *g_fnFireRel = NULL;
static void *g_fnCast = NULL, *g_fnChoose = NULL, *g_fnStartCast = NULL;
static void *g_fnStopCast = NULL, *g_fnCharFire = NULL, *g_fnCanCast = NULL;
static void *g_fnTrigger = NULL, *g_fnPickup = NULL, *g_fnCanPickup = NULL;
static void *g_fnUnPossess = NULL, *g_fnUnPossessAI = NULL, *g_fnPossessAI = NULL;
static void *g_fnStopTrail = NULL, *g_fnIdleWander = NULL, *g_fnRandIdle = NULL;
static void *g_fnStopTrailKW = NULL, *g_fnDropTrail = NULL;
static int   g_offFollowRadius = -1;  // KWAIController.TrailCharFollowRadius
static int   g_offLeadChar = -1;      // KWAIController.LeadChar
static int   g_offUseDistLead = -1;   // bUseDistFromLeadCharForMovement
static DWORD g_maskUseDistLead = 0;
static int   g_offTrailingChar = -1;  // KWPawn.TrailingChar (on the lead)
static int   g_offCurrentSpell = -1, g_offSpellTarget = -1;
static int   g_offProjSpeed = -1, g_offProjMaxSpeed = -1; // Engine.Projectile
static void *g_defaultSpell = NULL, *g_fnSpawnSpell = NULL, *g_fnHasSpell = NULL;
static void *g_fnPlayCast = NULL, *g_fnPlayCastAim = NULL, *g_fnFinalize = NULL;
static void *g_fnBlendOut = NULL, *g_fnIsAiming = NULL, *g_fnShowWeapon = NULL;
static void *g_fnCharRelFire = NULL, *g_fnChangeAnim = NULL, *g_fnPlayJump = NULL;
static void *g_fnAnimEnd = NULL, *g_fnPlayIdle = NULL;
static void *g_fnGotoGround = NULL;   // KWPawn.GotoDefaultGroundMovementState
static void *g_fnPlayWaiting = NULL, *g_fnSwitchFight = NULL, *g_fnSwitchNormal = NULL;
// v15 aim glow: the original player's sparkle-at-the-aim-point effect.
// Spawn/Destroy are NATIVES: they must be driven through synthesized FFrame
// bytecode (see spawnFX/destroyActorFX) - ProcessEvent cannot feed natives
// their parms in this engine, which is why the first attempt always got NULL.
static void *g_clsAimFX      = NULL;    // hgame.SpellCursorEmitter class
static void *g_texAimFX      = NULL;   // hgame.HP_FX.Particles.Sparkle_3
static void *g_texAimFXFr[8] = { 0 };  // sparkle frames for the cycling glow
static int   g_nAimFXFr      = 0;      // how many frames resolved
static int   g_offTexAimFX   = -1;     // Engine.Actor.Texture
static int   g_offStyleAimFX = -1;     // Engine.Actor.Style (byte enum)
static int   g_offDTAimFX    = -1;     // Engine.Actor.DrawType (byte enum)
static int   g_offScaleAimFX = -1;     // Engine.Actor.DrawScale (float)
static int   g_offUnlitAimFX = -1;     // Engine.Actor.bUnlit
static int   g_offEmbAimFX   = -1;     // Engine.Emitter.Emitters (TArray)
static int   g_offSSPAimFX   = -1;     // Engine.ParticleEmitter.StartSizeRange
static int   g_offOpaAimFX   = -1;     // Engine.ParticleEmitter.Opacity
static DWORD g_maskUnlitAimFX = 0;
static BOOL  g_aimDiag      = FALSE;   // dump_objects run: probe FX classes
static float g_dbgGlowScale = -1.0f;   // K-key A/B probe; <0 = normal pulse
static BOOL  g_aimDiagDone  = FALSE;
static int   g_offDeleteMe   = -1;      // Engine.Actor.bDeleteMe
static DWORD g_maskDeleteMe  = 0;
static int   g_offCtrlAnims = -1;     // Controller.bControlAnimations
static DWORD g_maskCtrlAnims = 0;    // bit inside that dword
static int   g_offDontPossess = -1;  // Pawn.bDontPossess
static DWORD g_maskDontPossess = 0;
static BOOL  g_fnResolved = FALSE;

// Set a UE2 packed bool. BitMask is recovered from the UBoolProperty object.
static BOOL setBoolProp(void *obj, int offset, DWORD mask, BOOL val)
{
    if (!obj || offset < 0 || !mask) return FALSE;
    if (IsBadReadPtr(obj, offset + 4) || IsBadWritePtr((BYTE *)obj + offset, 4))
        return FALSE;
    DWORD *slot = (DWORD *)((BYTE *)obj + (offset & ~3));
    if (val) *slot |= mask;
    else     *slot &= ~mask;
    return TRUE;
}

// Exact mask for v52 native FX: let the game's bool-property implementation
// copy all-one source bits into a zero scratch word. This yields BitMask
// without guessing among UProperty's link pointers/net indices. The supplied
// Core.dll CopySingleValue implementation only reads the property and touches
// these two local words; no actor/default object is mutated.
static DWORD nativeBoolBitMask(const char *path)
{
    typedef void (__fastcall *CopyBool)(void *,void *,void *,void *,void *);
    static CopyBool copy=NULL;
    if(!copy && g_core) copy=(CopyBool)GetProcAddress(g_core,
        "?CopySingleValue@UBoolProperty@@UBEXPAX0PAVUObject@@@Z");
    void *p=findObjectByPath(path); char name[240];
    if(!copy || !p || IsBadReadPtr(p,0x2C) ||
       strncmp(objName(p,name,sizeof(name)),"BoolProperty ",13)) return 0;
    DWORD dest=0,source=~(DWORD)0;
    copy(p,NULL,&dest,&source,NULL);
    return dest && !(dest & (dest-1)) ? dest : 0;
}

static DWORD boolBitMask(const char *path)
{
    void *p = findObjectByPath(path);
    if (!p || g_propOffsetField < 0 || IsBadReadPtr(p, g_propOffsetField + 0x40))
        return 0;
    // UBoolProperty::BitMask sits after UProperty::Offset. Scan for a power of two.
    for (int d = 4; d <= 0x38; d += 4) {
        DWORD m = *(DWORD *)((BYTE *)p + g_propOffsetField + d);
        if (m && (m & (m - 1)) == 0) return m;
    }
    return 1;
}

// What the renderer sees for an actor: bHidden, DrawType, Texture, DrawScale
// and the Emitter particle array. "Alive but invisible" is exactly the
// failure mode of a cursor emitter whose defaults carry no particles.
static void dumpActorVisuals(void *a, const char *tag)
{
    if (g_propOffsetField < 0) return; // don't cache failed pre-calibration lookups
    char actorName[240];
    if (!a || IsBadReadPtr(a,0x2C)) return;
    objName(a,actorName,sizeof(actorName));
    if (!strncmp(actorName,"SpriteEmitter ",14) || !strncmp(actorName,"ParticleEmitter ",16) ||
        !strncmp(actorName,"MeshEmitter ",12)) {
        logf_("    %s: particle UObject (not an Actor)",actorName); return;
    }
    BOOL isEmitterActor=!strncmp(actorName,"SpellCursorEmitter ",19) ||
        !strncmp(actorName,"SpellGesture ",13) || !strncmp(actorName,"Emitter ",8) ||
        !strncmp(actorName,"SpellFlyEmitter ",16);
    static int offHidden = -2, offDrawType = -2, offTexture = -2,
               offScale = -2, offEmitters = -2, offStyle = -2,
               offUnlit = -2, offLoc = -2;
    static DWORD maskHidden = 0, maskUnlit = 0;
    if (offHidden == -2) {
        offHidden   = propOffset("Engine.Actor.bHidden");
        maskHidden  = boolBitMask("Engine.Actor.bHidden");
        offDrawType = propOffset("Engine.Actor.DrawType");
        offTexture  = propOffset("Engine.Actor.Texture");
        offScale    = propOffset("Engine.Actor.DrawScale");
        offEmitters = propOffset("Engine.Emitter.Emitters");
        offStyle    = propOffset("Engine.Actor.Style");
        offUnlit    = propOffset("Engine.Actor.bUnlit");
        maskUnlit   = boolBitMask("Engine.Actor.bUnlit");
        offLoc      = propOffset("Engine.Actor.Location");
    }
    char b[160];
    if (!a || IsBadReadPtr(a, 0x100)) { logf_("    %-24s <unreadable>", tag); return; }
    objName(a, b, sizeof(b));
    logf_("    %-24s %s", tag, b);
    if (offHidden > 0 && maskHidden && !IsBadReadPtr(a, offHidden + 4))
        logf_("      bHidden=%d",
              (*(DWORD *)((BYTE *)a + (offHidden & ~3)) & maskHidden) != 0);
    if (offStyle > 0 && !IsBadReadPtr(a, offStyle + 4))
        logf_("      Style=%u", (unsigned)*(BYTE *)((BYTE *)a + offStyle));
    if (offUnlit > 0 && maskUnlit && !IsBadReadPtr(a, offUnlit + 4))
        logf_("      bUnlit=%d",
              (*(DWORD *)((BYTE *)a + (offUnlit & ~3)) & maskUnlit) != 0);
    if (offLoc > 0 && !IsBadReadPtr(a, offLoc + 12)) {
        float *al = (float *)((BYTE *)a + offLoc);
        logf_("      Location=(%.0f %.0f %.0f)", al[0], al[1], al[2]);
    }
    if (offDrawType > 0 && !IsBadReadPtr(a, offDrawType + 4))
        logf_("      DrawType=%u", (unsigned)*(BYTE *)((BYTE *)a + offDrawType));
    if (offTexture > 0 && !IsBadReadPtr(a, offTexture + 4)) {
        void *tex = *(void **)((BYTE *)a + offTexture);
        logf_("      Texture=%s", tex ? objName(tex, b, sizeof(b)) : "(none)");
    }
    if (offScale > 0 && !IsBadReadPtr(a, offScale + 4))
        logf_("      DrawScale=%.2f", *(float *)((BYTE *)a + offScale));
    static int offCursorParts = -2, offParticleType = -2,
               offCtrlCursor = -2;
    if (offCursorParts == -2) {
        offCursorParts   = propOffset("hgame.SpellCursor.CursorParticles");
        offParticleType  = propOffset("hgame.SpellCursor.particleType");
        offCtrlCursor    = propOffset("KWGame.KWHeroController.Cursor");
    }
    if (isEmitterActor && offEmitters > 0 && !IsBadReadPtr(a, offEmitters + 12)) {
        void **data = *(void ***)((BYTE *)a + offEmitters);
        int num = *(int *)((BYTE *)a + offEmitters + 4);
        int max = *(int *)((BYTE *)a + offEmitters + 8);
        // Only trust the array when it looks like one: a non-Emitter actor
        // read at the Emitters offset yields garbage Num/Max, and walking
        // that Data pointer hangs GetFullName (froze two diag runs).
        if (num > 0 && num <= 64 && max > 0 && max <= 64) {
            logf_("      Emitters: Data=%p Num=%d Max=%d", (void *)data, num, max);
            for (int e = 0; e < num && e < 3; e++)
                if (data && !IsBadReadPtr(data, 4 * (e + 1)) && data[e])
                    logf_("        [%d] %s", e, objName(data[e], b, sizeof(b)));
        } else {
            logf_("      Emitters: empty or invalid (Num=%d Max=%d)", num, max);
        }
    }
    // SpellCursor-family fields - only meaningful on SpellCursor actors
    // (class token is objName's first word), so gate on it.
    {
        const char *spc = strchr(actorName, ' ');
        size_t clt = spc ? (size_t)(spc - actorName) : 0;
        BOOL isSpellCursor = (clt == 11 && !strncmp(actorName, "SpellCursor", 11));
        BOOL isController  = (clt > 10 && !strncmp(actorName + clt - 10,"Controller",10));
        if (isSpellCursor && offCursorParts > 0 &&
            !IsBadReadPtr(a, offCursorParts + 4)) {
            void *cp = *(void **)((BYTE *)a + offCursorParts);
            logf_("      CursorParticles=%p%s", cp,
                  (cp && !IsBadReadPtr(cp, 8)) ? objName(cp, b, sizeof(b)) : "");
        }
        if (isSpellCursor && offParticleType > 0 &&
            !IsBadReadPtr(a, offParticleType + 4)) {
            void *pt = *(void **)((BYTE *)a + offParticleType);
            logf_("      particleType=%p%s", pt,
                  (pt && !IsBadReadPtr(pt, 8)) ? objName(pt, b, sizeof(b)) : "");
        }
        if (isController && offCtrlCursor > 0 &&
            !IsBadReadPtr(a, offCtrlCursor + 4)) {
            void *cc = *(void **)((BYTE *)a + offCtrlCursor);
            logf_("      ctrl.Cursor=%p%s", cc,
                  (cc && !IsBadReadPtr(cc, 8)) ? objName(cc, b, sizeof(b)) : "");
        }
    }
    if (g_offDeleteMe > 0 && g_maskDeleteMe &&
        !IsBadReadPtr(a, g_offDeleteMe + 4))
        logf_("      bDeleteMe=%d",
              (*(DWORD *)((BYTE *)a + (g_offDeleteMe & ~3)) & g_maskDeleteMe) != 0);
}

// Live cursor ACTORS (first objName token = class name): does the game keep
// a persistent cursor actor we could learn from (or reuse)? Matching on the
// class token keeps States/Structs/Properties out of the visual dump.
static void dumpCursorInstances(void)
{
    if (!g_objArray) return;
    int n = g_objArray->Num; if (n <= 0 || n > 400000) return;
    char buf[300]; int shown = 0;
    logf_("--- live cursor-ish objects ---");
    for (int i = 0; i < n && shown < 24; i++) {
        void *o = g_objArray->Data[i]; if (!o) continue;
        objName(o, buf, sizeof(buf));
        const char *sp = strchr(buf, ' '); if (!sp) continue;
        size_t cl = sp - buf;
        if (!(cl == 11 && !strncmp(buf, "SpellCursor", 11)) &&
            !(cl == 17 && !strncmp(buf, "SpellCursorEmitter", 17)) &&
            !(cl == 12 && !strncmp(buf, "SelectCursor", 12)) &&
            !(cl == 8  && !strncmp(buf, "KWCursor", 8)) &&
            !(cl == 7  && !strncmp(buf, "HCursor", 7)) &&
            !(cl == 17 && !strncmp(buf, "KWHeroController", 17)) &&
            !(cl == 17 && !strncmp(buf, "HPHeroController", 17)) &&
            !(cl == 18 && !strncmp(buf, "HPPlayerController", 18))) continue;
        if (!actorInCurrentLevel(o)) continue;
        dumpActorVisuals(o, buf);
        shown++;
    }
    logf_("--- %d live cursor objects ---", shown);
}

// Cursor/emitter knowledge for the aim glow: classes, full property lists of
// the cursor classes and of Engine.Emitter, and any cursor-named property.
static void dumpCursorSigs(void)
{
    logf_("--- cursor/emitter diagnostics ---");
    dumpClassesMatching("Cursor");
    dumpFunctionSig("KWGame.KWHeroController.makeCursor");
    dumpClassPropsFull("hgame.SpellCursorEmitter");
    dumpClassPropsFull("hgame.SpellCursor");
    dumpClassPropsFull("hgame.HCursor");
    dumpClassPropsFull("KWGame.HCursor");
    dumpClassPropsFull("Engine.Emitter");
    dumpPropsAny("Cursor");
    dumpCursorInstances();
    logf_("--- cursor diagnostics end ---");
}

static void resolveActions(void)
{
    if (g_fnResolved) return;
    g_fnResolved = TRUE;
    g_fnJump    = findObjectByPath("KWGame.KWPawn.DoJump_Player");
    if (!g_fnJump) g_fnJump = findObjectByPath("Engine.Pawn.DoJump");
    g_fnFire    = findObjectByPath("KWGame.KWPawn.PressedFire");
    g_fnFireRel = findObjectByPath("KWGame.KWPawn.ReleasedFire");
    g_fnCast      = findObjectByPath("hgame.HPCharacter.castSpell");
    g_fnChoose    = findObjectByPath("hgame.HPCharacter.ChooseSpell");
    g_fnStartCast = findObjectByPath("hgame.HPCharacter.StartCasting");
    g_fnStopCast  = findObjectByPath("hgame.HPCharacter.StopCasting");
    g_fnCharFire  = findObjectByPath("hgame.HPCharacter.Fire");
    g_fnCanCast   = findObjectByPath("hgame.HPCharacter.canCast");
    g_fnTrigger   = findObjectByPath("KWGame.KWPawn.Trigger");
    if (!g_fnTrigger) g_fnTrigger = findObjectByPath("Engine.Pawn.Trigger");
    g_fnPickup    = findObjectByPath("KWGame.KWPawn.PickupActor");
    g_fnCanPickup = findObjectByPath("KWGame.KWPawn.CanDoPickupActor");
    g_fnUnPossess = findObjectByPath("Engine.Controller.UnPossess");
    // The companion's controller OVERRIDES UnPossess. ProcessEvent dispatches
    // exactly the UFunction it is handed, so calling the base version runs a
    // partial teardown - always prefer the most-derived override.
    g_fnUnPossessAI = findObjectByPath("hgame.HPCompanionController.UnPossess");
    if (!g_fnUnPossessAI)
        g_fnUnPossessAI = findObjectByPath("hgame.HPAIController.UnPossess");
    g_fnPossessAI   = findObjectByPath("hgame.HPAIController.Possess");
    g_fnStopTrail   = findObjectByPath("hgame.HPCompanionController.OnStopTrailingLeadChar");
    g_fnStopTrailKW = findObjectByPath("KWGame.KWAIController.StopTrailingLeadChar");
    if (!g_fnStopTrailKW)
        g_fnStopTrailKW = findObjectByPath("KWGame.KWAIController.OnStopTrailingLeadChar");
    g_fnDropTrail   = findObjectByPath("KWGame.KWPawn.DropTrailingChar");
    g_offFollowRadius = propOffset("KWGame.KWAIController.TrailCharFollowRadius");
    g_offLeadChar     = propOffset("KWGame.KWAIController.LeadChar");
    g_offUseDistLead  = propOffset("KWGame.KWAIController.bUseDistFromLeadCharForMovement");
    g_maskUseDistLead = boolBitMask("KWGame.KWAIController.bUseDistFromLeadCharForMovement");
    g_offTrailingChar = propOffset("KWGame.KWPawn.TrailingChar");
    logf_("[leash] StopTrailKW=%p DropTrail=%p FollowRadius=+0x%X LeadChar=+0x%X "
          "UseDistLead=+0x%X mask=0x%lX TrailingChar=+0x%X",
          g_fnStopTrailKW, g_fnDropTrail, g_offFollowRadius, g_offLeadChar,
          g_offUseDistLead, (unsigned long)g_maskUseDistLead, g_offTrailingChar);
    g_fnIdleWander  = findObjectByPath("hgame.HPAIController.GotoIdleWanderStateFromTrailLeadChar");
    g_fnRandIdle    = findObjectByPath("hgame.HPAIController.GotoStateRandomIdleAnim");
    g_fnSpawnSpell = findObjectByPath("hgame.HPPawn.SpawnSpell");
    g_fnHasSpell   = findObjectByPath("hgame.HPCharacter.HasSpell");
    g_fnPlayCast     = findObjectByPath("hgame.HPCharacter.playCast");
    g_fnPlayCastAim  = findObjectByPath("hgame.HPCharacter.playCastAim");
    g_fnFinalize     = findObjectByPath("hgame.HPCharacter.finalizeSpell");
    g_fnBlendOut     = findObjectByPath("hgame.HPCharacter.blendOutCast");
    g_fnIsAiming     = findObjectByPath("hgame.HPCharacter.IsAimingOrCasting");
    g_fnShowWeapon   = findObjectByPath("hgame.HPCharacter.ShowWeapon");
    g_fnCharRelFire  = findObjectByPath("hgame.HPCharacter.ReleasedFire");
    g_fnChangeAnim   = findObjectByPath("KWGame.KWPawn.ChangeAnimation");
    g_fnAnimEnd      = findObjectByPath("hgame.HPCharacter.AnimEnd");
    g_fnPlayIdle     = findObjectByPath("KWGame.KWPawn.PlayIdle");
    g_fnGotoGround   = findObjectByPath("KWGame.KWPawn.GotoDefaultGroundMovementState");
    g_fnPlayJump     = findObjectByPath("KWGame.KWPawn.PlayJump");
    g_fnPlayWaiting  = findObjectByPath("KWGame.KWPawn.PlayWaiting");
    g_fnSwitchFight  = findObjectByPath("hgame.HPCharacter.SwitchToFightStanceAnims");
    g_fnSwitchNormal = findObjectByPath("hgame.HPCharacter.SwitchToNormalStanceAnims");
    g_offCurrentSpell = propOffset("hgame.HPCharacter.currentSpell");
    g_offSpellTarget  = propOffset("hgame.HPCharacter.spellTarget");
    g_offProjSpeed    = propOffset("Engine.Projectile.Speed");
    g_offProjMaxSpeed = propOffset("Engine.Projectile.MaxSpeed");
    g_offCtrlAnims    = propOffset("Engine.Controller.bControlAnimations");
    g_maskCtrlAnims   = boolBitMask("Engine.Controller.bControlAnimations");
    g_offDontPossess  = propOffset("Engine.Pawn.bDontPossess");
    g_maskDontPossess = boolBitMask("Engine.Pawn.bDontPossess");
    g_clsAimFX      = findObjectByPath("hgame.SpellCursorEmitter");
    // v16: the particle path (DrawType=10) never showed on hardware even
    // with a configured SpriteEmitter - this build's GameFX rendering seems
    // to need game-side registration. Render the glow as a plain sprite
    // instead (the same path the game's own hidden SpellCursor0 uses):
    // the game's own sparkle texture, translucent, unlit, softly pulsing.
    g_texAimFX      = findObjectByPath("hgame.HP_FX.Particles.Sparkle_3");
    // v17: sparkle frames - cycle them while aiming so the marker actually
    // sparkles instead of sitting as one flat image.
    for (int s = 1; s <= 8; s++) {
        char path[80];
        _snprintf(path, sizeof(path), "hgame.HP_FX.Particles.Sparkle_%d", s);
        void *t = findObjectByPath(path);
        if (t && g_nAimFXFr < 8) {
            g_texAimFXFr[g_nAimFXFr++] = t;
            char tb[160];
            logf_("[aimfx] sparkle frame: %s", objName(t, tb, sizeof(tb)));
        }
    }
    if (!g_nAimFXFr && g_texAimFX) g_texAimFXFr[g_nAimFXFr++] = g_texAimFX;
    g_offTexAimFX   = propOffset("Engine.Actor.Texture");
    g_offStyleAimFX = propOffset("Engine.Actor.Style");
    g_offDTAimFX    = propOffset("Engine.Actor.DrawType");
    g_offScaleAimFX = propOffset("Engine.Actor.DrawScale");
    g_offUnlitAimFX = propOffset("Engine.Actor.bUnlit");
    g_offEmbAimFX   = propOffset("Engine.Emitter.Emitters");
    g_offSSPAimFX   = propOffset("Engine.ParticleEmitter.StartSizeRange");
    g_offOpaAimFX   = propOffset("Engine.ParticleEmitter.Opacity");
    g_maskUnlitAimFX = boolBitMask("Engine.Actor.bUnlit");
    g_offDeleteMe   = propOffset("Engine.Actor.bDeleteMe");
    g_maskDeleteMe  = boolBitMask("Engine.Actor.bDeleteMe");
    logf_("[actions] playCast=%p playCastAim=%p finalize=%p blendOut=%p "
          "IsAiming=%p ShowWeapon=%p ChangeAnim=%p PlayJump=%p",
          g_fnPlayCast, g_fnPlayCastAim, g_fnFinalize, g_fnBlendOut,
          g_fnIsAiming, g_fnShowWeapon, g_fnChangeAnim, g_fnPlayJump);
    logf_("[actions] bControlAnimations offset=+0x%X mask=0x%lX "
          "bDontPossess offset=+0x%X mask=0x%lX",
          g_offCtrlAnims, (unsigned long)g_maskCtrlAnims,
          g_offDontPossess, (unsigned long)g_maskDontPossess);
    logf_("[actions] DoJump=%p PressedFire=%p ReleasedFire=%p ProcessEvent=%p",
          g_fnJump, g_fnFire, g_fnFireRel, (void *)g_ProcessEvent);
    logf_("[actions] aimfx: execSpawn=%p execDestroy=%p NameConst=%d "
          "SpellCursorEmitter=%p bDeleteMe=+0x%X/0x%lX",
          (void *)g_execSpawn, (void *)g_execDestroy, g_opNameConst, g_clsAimFX,
          g_offDeleteMe, (unsigned long)g_maskDeleteMe);
    {
        char tb[160];
        logf_("[actions] aimfx sparkle frames=%d AnimEnd=%p PlayIdle=%p GotoGround=%p",
              g_nAimFXFr, g_fnAnimEnd, g_fnPlayIdle, g_fnGotoGround);
        logf_("[actions] aimfx visual: tex=%s texOff=+0x%X styleOff=+0x%X "
              "dtOff=+0x%X scaleOff=+0x%X unlit=+0x%X/0x%lX",
              g_texAimFX ? objName(g_texAimFX, tb, sizeof(tb)) : "<MISSING>",
              g_offTexAimFX, g_offStyleAimFX, g_offDTAimFX, g_offScaleAimFX,
              g_offUnlitAimFX, (unsigned long)g_maskUnlitAimFX);
    }
    logf_("[actions] castSpell=%p ChooseSpell=%p StartCasting=%p StopCasting=%p "
          "HPChar.Fire=%p canCast=%p", g_fnCast, g_fnChoose, g_fnStartCast,
          g_fnStopCast, g_fnCharFire, g_fnCanCast);
    {   // castSpell takes a ClassProperty, so a real spell class is required.
        // Companions start with currentSpell == NULL, which is precisely why
        // PressedFire produced no visible effect.
        static const char *spells[] = {
            "hgame.RictusempraSpell", "hgame.DepulsoSpell", "hgame.SpongifySpell",
            "hgame.LumosSpell", "hgame.AlohomoraSpell", "hgame.GlaciusSpell",
            "hgame.IncendioSpell", "hgame.LapiforsSpell", "hgame.baseSpell", NULL };
        char b[160];
        for (int i = 0; spells[i]; i++) {
            void *c = findObjectByPath(spells[i]);
            logf_("    spell class %-26s -> %p", spells[i], c);
            if (c && !g_defaultSpell) g_defaultSpell = c;
        }
        // Whatever the lead character currently has selected beats our guess.
        void *lead = findActorByClass("harry");
        if (lead && g_offCurrentSpell > 0 && !IsBadReadPtr(lead, g_offCurrentSpell + 4)) {
            void *cs = *(void **)((BYTE *)lead + g_offCurrentSpell);
            logf_("    lead(harry) currentSpell = %s",
                  cs ? objName(cs, b, sizeof(b)) : "<null>");
            if (cs) g_defaultSpell = cs;
        }
        logf_("    ==> default spell class = %s",
              g_defaultSpell ? objName(g_defaultSpell, b, sizeof(b)) : "<NONE>");
    }
    logf_("[actions] UnPossess(base)=%p UnPossess(HPAIController)=%p "
          "StopTrailing=%p IdleWander=%p RandomIdle=%p", g_fnUnPossess,
          g_fnUnPossessAI, g_fnStopTrail, g_fnIdleWander, g_fnRandIdle);
    logf_("[actions] Trigger=%p PickupActor=%p CanDoPickup=%p UnPossess=%p "
          "currentSpell=+0x%X spellTarget=+0x%X", g_fnTrigger, g_fnPickup,
          g_fnCanPickup, g_fnUnPossess, g_offCurrentSpell, g_offSpellTarget);
}

// Invoke a script function on a pawn. Params block is zeroed and generously
// oversized so any small signature is satisfied safely.
static void callFn(void *obj, void *fn, const char *tag)
{
    if (!obj || !fn || !g_ProcessEvent) return;
    BYTE parms[256];
    memset(parms, 0, sizeof(parms));
    logf_("    -> ProcessEvent %s", tag);
    g_ProcessEvent(obj, NULL, fn, parms, NULL);
}

// Script field offsets, all resolved BY NAME via UProperty::Offset.
static struct {
    BOOL ok;
    int  Location, Rotation, Velocity, Acceleration, Physics;
    int  PawnController;      // Pawn.Controller
    int  ControllerPawn;      // Controller.Pawn
    int  GroundSpeed, JumpZ, AccelRate;
    int  LifeSpan, Base, Floor;
    int  DesiredRotation, RotationRate;   // v12: pin during standing jump
} F = {};

// Same, but with a caller-supplied parameter block (and it reports the block
// back, so return values can be read out of it).
static void callFnP(void *obj, void *fn, void *parms, int size, const char *tag)
{
    if (!obj || !fn || !g_ProcessEvent) { logf_("    !! %s unavailable", tag); return; }
    g_ProcessEvent(obj, NULL, fn, parms, NULL);
    char h[128]; int c = 0;
    for (int i = 0; i < size && i < 24 && c < 100; i++)
        c += sprintf(h + c, "%02X ", ((BYTE *)parms)[i]);
    logf_("    -> %s  parms[%s]", tag, h);
}

// Case-insensitive substring helper kept local to the action layer (a later
// input helper has its own nameHas()).
static BOOL ciHas(const char *s, const char *frag)
{
    if (!s || !frag) return FALSE;
    size_t n = strlen(s), f = strlen(frag);
    if (!f || f > n) return FALSE;
    for (size_t i = 0; i + f <= n; i++) {
        size_t k = 0;
        for (; k < f; k++) {
            char a = s[i + k], b = frag[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (k == f) return TRUE;
    }
    return FALSE;
}

static BOOL objectClassToken(void *obj, char *out, int cch)
{
    if (!out || cch <= 0) return FALSE;
    out[0] = 0;
    char full[256];
    objName(obj, full, sizeof(full));
    const char *sp = strchr(full, ' ');
    if (!sp || sp == full) return FALSE;
    int n = (int)(sp - full);
    if (n >= cch) n = cch - 1;
    memcpy(out, full, n); out[n] = 0;
    return TRUE;
}

static BOOL functionPath(void *fn, char *out, int cch)
{
    if (!fn || !out || cch <= 0) return FALSE;
    char full[300];
    objName(fn, full, sizeof(full));
    const char *sp = strchr(full, ' ');
    if (!sp || strncmp(full, "Function", 8)) return FALSE;
    strncpy(out, sp + 1, cch - 1); out[cch - 1] = 0;
    return TRUE;
}

// ===========================================================================
// v51: CAST GAMEPLAY, PLAYER-1 FAITHFUL.
//
// How the original player's cast activates statues, pads, doors and lesson
// objects (KnowWonder KWGame framework, confirmed against the Shrek 2 KWGame
// export and the HP2 script decompile - both the same engine family):
//
//   1. TARGETING: every KW actor carries a "Reaction/Targeting" property
//      block: vulnerableToClass (Class<Projectile> - the SPELL CLASS the
//      object reacts to), SizeModifier, CentreOffset, GestureDistance,
//      bMustClickToTarget. The SelectCursor/SpellCursor traces the camera
//      line and accepts only actors whose vulnerableToClass != None.
//   2. SPELL CHOICE: the spell is CHOSEN FROM THE TARGET (baseWand.
//      ChooseSpell(target.eVulnerableToSpell) in HP2 -> the class stored on
//      the object in HP3). HP3 has no manual spell selection for a reason.
//   3. ACTIVATION: the projectile flies with TargetActor set (homing) and on
//      collision runs spell.ProcessTouch(Other, HitLocation), which calls the
//      object's HandleSpell<Name>(spell, hitLoc) / OnSpellHit(spell, hitLoc)
//      handler; SpellTrigger-family actors fire through Trigger.Touch(spell)
//      + IsRelevant (spell type must match). Close targets get an immediate
//      spell.ProcessTouch(target, target.Location) ("autohit").
//
// v50 had none of this: it fired the DEFAULT spell (Rictusempra) with no
// TargetActor, only recognised actors literally named "SpellTrigger", and
// its fallback was a plain Trigger(caster, caster) - which a spell trigger
// ignores. Statues and jump pads therefore never qualified, never got the
// right spell class, and never received a spell touch.
//
// v51 replicates the real path with reflection only (no source needed):
//   * one pass over the object table indexes the vulnerable/targeting
//     properties and the spell-handler functions of every class, and
//     calibrates UObject::Class + UStruct::SuperField so an actor's class
//     chain can be walked (cgClassInfo);
//   * candidates = actors whose vulnerableToClass value is a live class
//     (plus SpellTrigger-family actors); the release ray picks the one under
//     the camera spell line (CentreOffset/SizeModifier honoured, LOS
//     checked), exactly like the cursor;
//   * the spell class comes from the target (ChooseSpell/StartCasting/
//     castSpell/SpawnSpell all get it), the target goes in as TargetActor;
//   * after the projectile leaves, a pending "hit" watches it: when it
//     reaches the target (or the flight deadline passes with it still
//     alive) the mod runs the game's own spell.ProcessTouch(target, loc) -
//     the autohit path - so the object's handler runs through the game's
//     code, not ours. Trigger-family targets also get Touch(spell).
//   * everything is logged as [castgame] / [castgame-scan] / [castgame-fn]
//     so a hardware log shows the class, the property, the chosen spell,
//     the flight and the exact handler that ran.
// ===========================================================================
#define CG_MAX_IDX   2560
#define CG_MAX_CLS   1024
#define CG_MAX_CAND  384
#define CG_MAX_KCLS  4096

enum { CGK_VULNCLS = 1, CGK_VULNBYTE, CGK_SPELLCLS, CGK_SIZE, CGK_CENTRE,
       CGK_FN_HANDLE, CGK_FN_PTOUCH, CGK_FN_TOUCH, CGK_FN_TRIGGER };
enum { CGC_NONE = 0, CGC_VULN, CGC_VULNBYTE, CGC_TRIG, CGC_FN, CGC_NAME };

struct CgIdx { void *obj; int kind; int off; DWORD declHash; BOOL stateFn;
               char decl[80]; char name[40]; };
struct CgClass {
    void *cls; char token[48]; char path[96];
    BOOL isActor, isPawn, isHero, isProjectile, isTrigger, nameSpellTrigger,
         nameTrigger;
    int  vulnOff, vulnByteOff, spellClsOff, sizeOff, centreOff;
    char vulnName[40];
    void *fnPTouch, *fnTouch, *fnTrigger;
    char  pTouchDecl[80], touchDecl[80];
    BOOL  handlerSpellTried;
    void *fnGen[4];  char genName[4][40];  char genDecl[4][80];  BOOL genState[4];  int nGen;  // OnSpellHit..
    void *fnSpec[8]; char specName[8][40]; char specDecl[8][80]; BOOL specState[8]; int nSpec; // HandleSpell<Name>
    void *handlerSpell;   // spell class implied by the HandleSpell<Name> handlers
    char  handlerSpellFrom[40];
};
struct CgCand {
    void *obj; int slot; char name[96]; CgClass *info; void *spellCls;
    int kind; float loc[3];
};
struct CgPending {
    BOOL active; int player;
    void *spell; int spellSlot; char spellName[96]; void *spellClsPtr;
    void *target; int targetSlot; char targetName[96]; CgClass *tinfo;
    void *spellCls; void *caster;
    DWORD fireAt, deadline; float hitR; float aim[3]; float d0;
};
struct CgFnLayout {
    void *fn; int n; int total;
    // complete means every direct reflected property was inspected. Unknown
    // is deliberately sticky so safety-sensitive calls cannot mistake an
    // unsupported property kind for a no-argument function.
    BOOL complete, unknown;
    struct { int off; int kind; char name[32]; } p[12];
};

static CgIdx    g_cgIdx[CG_MAX_IDX];  static int g_cgNIdx = 0;
static CgClass  g_cgCls[CG_MAX_CLS];  static int g_cgNCls = 0;
static CgCand   g_cgCand[CG_MAX_CAND]; static int g_cgNCand = 0;
static void    *g_cgKCls[CG_MAX_KCLS]; static int g_cgNKCls = 0;
static CgPending g_cgPend[8];
static CgFnLayout g_cgLay[24]; static int g_cgNLay = 0;
static BOOL  g_cgReady = FALSE, g_cgChainOK = FALSE, g_cgScanLogged = FALSE;
static DWORD g_cgScanAt = 0;
static int   g_offObjClass = -1, g_offSuper = -1;
static void *g_clsActor = NULL, *g_clsPawn = NULL, *g_clsHPChar = NULL,
            *g_clsTrigger = NULL, *g_clsProjectile = NULL;
static int   g_cgNVulnDecl = 0, g_cgNHandlerDecl = 0;
static void *g_cgBeginCls[8] = {0};      // spell class chosen at aim start
static char  g_cgLockName[8][96];        // aim-glow lock change logging
static BOOL  g_castAutoHit = TRUE;       // ini [actions] CastAutoHit
static int   g_cgPelogExtra = 0;         // spell/handler events per window
static int   g_cgP1Windows = 0;          // telemetry windows opened for P1 casts (per level)
static BOOL  g_pelogOn    = FALSE;       // ProcessEvent capture window (see pelog)

// v54/v55 does not guess a class name for a three-person interaction. It learns
// a class from live game behaviour, and the resulting class proof is kept only
// for this level and is the sole admission token for P2/P3's delayed fallback.
//   * split OFF: the stock demonstration - P1's cursor locked on the object AND
//     both genuine companions reporting that exact object as their spell target;
//   * split ON : P1's genuine stock cursor lock on the object (companions are
//     AI-driven in-session and cannot show a shared pawn spellTarget, so the
//     all-three field test is not required there).
struct CoopClassProof {
    void *cls;
    char path[96];
    DWORD observedAt;
};
static CoopClassProof g_coopProof[8] = {};
static int g_nCoopProof = 0;
static void *g_coopProofPendingTarget = NULL, *g_coopProofPendingClass = NULL;
static DWORD g_coopProofPendingSince = 0;
// v56: P1's cursor lock also fires the cooperative path on its own. A real
// game cursor can flicker its aCurrentTarget through None for a frame or two
// between controller updates, so we record the target pointer + the last time
// we observed it and treat a brief gap (<= kP1CursorGrace) as the same lock.
// A longer gap, a different object, or an invalid pointer resets the dwell.
static void *g_p1CursorLockedTarget = NULL;
static DWORD g_p1CursorLockedAt    = 0;   // GetTickCount of first-seen-at
static DWORD g_p1CursorLastSeen   = 0;   // last tick we saw any non-NULL t
static int   g_p1CursorFired       = 0;   // 1 once coopStart was called here
static const DWORD kP1CursorGrace = 250u;
// v57: the same flicker tolerance for P2/P3 held-cast target samples (the
// aim picker can lose the reticle target for a frame between updates without
// the holder having released anything).
static const DWORD kCoopFlickerGrace = 250u;
// Some stock cooperative actors intentionally have no vulnerableToClass and
// therefore never enter g_cgCand. Keep a small, current-level cache of only
// behaviour-certified actors so they can still be selected with the same
// ray/LOS checks, without widening ordinary target discovery.
#define COOP_DISCOVERED_MAX 64
static CgCand g_coopDiscovered[COOP_DISCOVERED_MAX];
static int g_nCoopDiscovered = 0;
static DWORD g_coopDiscoveredAt = 0;
static BOOL coopClassMatchesProof(void *cls);
static void coopObserveP1(void *p1, void *cursorTarget);
static void coopP1HoldTry(void *p1);
// v56: cgWatchP1 also drives the coop hold for P1; coopTick itself is
// defined further down with the rest of the cooperative machinery.
static void coopTick(int i, void *pawn, BOOL held);

static void cgResetLevel(void)
{
    g_cgReady = FALSE; g_cgChainOK = FALSE; g_cgScanLogged = FALSE;
    g_cgNIdx = g_cgNCls = g_cgNCand = g_cgNKCls = g_cgNLay = 0;
    g_cgScanAt = 0; g_offObjClass = g_offSuper = -1;
    g_clsActor = g_clsPawn = g_clsHPChar = g_clsTrigger = g_clsProjectile = NULL;
    g_cgNVulnDecl = g_cgNHandlerDecl = 0;
    memset(g_cgPend, 0, sizeof(g_cgPend));
    memset(g_cgBeginCls, 0, sizeof(g_cgBeginCls));
    memset(g_cgLockName, 0, sizeof(g_cgLockName));
    memset(g_coopProof, 0, sizeof(g_coopProof));
    g_nCoopProof = 0;
    g_coopProofPendingTarget = g_coopProofPendingClass = NULL;
    g_coopProofPendingSince = 0;
    g_p1CursorLockedTarget = NULL;
    g_p1CursorLockedAt = g_p1CursorLastSeen = 0;
    g_p1CursorFired = 0;
    memset(g_coopDiscovered, 0, sizeof(g_coopDiscovered));
    g_nCoopDiscovered = 0; g_coopDiscoveredAt = 0;
    g_cgP1Windows = 0;
}

static const char *cgLastComp(const char *path)
{
    const char *d = strrchr(path, '.');
    return d ? d + 1 : path;
}
static DWORD cgHash(const char *s)
{
    DWORD h = 5381;
    while (*s) h = ((h << 5) + h) ^ (BYTE)*s++;
    return h;
}

// Known-class set: the ONLY pointers we ever treat as UClass objects. A
// property value or a struct field is never dereferenced through the name
// helpers unless it is one of these (GetFullName on garbage would crash).
static int cgKClsCmp(const void *a, const void *b)
{
    const void *x = *(void *const *)a, *y = *(void *const *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}
static BOOL cgIsKnownClass(void *p)
{
    if (!p || g_cgNKCls <= 0) return FALSE;
    int lo = 0, hi = g_cgNKCls - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (g_cgKCls[mid] == p) return TRUE;
        if (g_cgKCls[mid] < p) lo = mid + 1; else hi = mid - 1;
    }
    return FALSE;
}
static void *cgClassOf(void *o)
{
    if (g_offObjClass < 0 || !o || IsBadReadPtr(o, g_offObjClass + 4)) return NULL;
    void *c = *(void **)((BYTE *)o + g_offObjClass);
    return cgIsKnownClass(c) ? c : NULL;
}
static void *cgSuper(void *c)
{
    if (g_offSuper < 0 || !c || IsBadReadPtr(c, g_offSuper + 4)) return NULL;
    void *s = *(void **)((BYTE *)c + g_offSuper);
    return (s != c && cgIsKnownClass(s)) ? s : NULL;
}

// Find the one dword offset at which every (instance, expected pointer) pair
// agrees. Same calibration idea as UProperty::Offset (FINDINGS section 4).
static int cgCalibratePtrField(void *inst[], void *want[], int n, int maxOff,
                               const char *what)
{
    int cand[4][48], nc[4] = {0, 0, 0, 0}, used = 0;
    for (int k = 0; k < n && k < 4; k++) {
        if (!inst[k] || !want[k] || IsBadReadPtr(inst[k], maxOff + 4)) continue;
        for (int off = 0; off <= maxOff && nc[used] < 48; off += 4)
            if (*(void **)((BYTE *)inst[k] + off) == want[k]) cand[used][nc[used]++] = off;
        if (nc[used]) used++;
    }
    if (used == 0) { logf_("  [castgame] %s: no calibration pairs available", what); return -1; }
    int found = -1, hits = 0;
    for (int a = 0; a < nc[0]; a++) {
        BOOL all = TRUE;
        for (int k = 1; k < used && all; k++) {
            BOOL any = FALSE;
            for (int b = 0; b < nc[k]; b++) if (cand[k][b] == cand[0][a]) { any = TRUE; break; }
            all = any;
        }
        if (all) { found = cand[0][a]; hits++; }
    }
    if (hits != 1) {
        logf_("  [castgame] %s: %d consistent offsets across %d pairs - unusable",
              what, hits, used);
        return -1;
    }
    logf_("  [castgame] %s = +0x%X (%d pairs agree)", what, found, used);
    return found;
}

// One pass over the object table: class set, calibration anchors, and the
// property/function index the whole feature runs on.
static void cgBuildIndex(void)
{
    if (g_cgReady) return;
    if (g_propOffsetField < 0) calibratePropertyOffset();
    if (g_propOffsetField < 0) {
        static DWORD sSaidAt = 0;
        if (GetTickCount() - sSaidAt > 10000) {
            sSaidAt = GetTickCount();
            logf_("  [castgame] property offsets not calibrated yet - index deferred");
        }
        return;
    }
    g_cgReady = TRUE;
    g_cgNIdx = g_cgNCls = g_cgNKCls = g_cgNLay = 0; g_cgNCand = 0;
    g_cgNVulnDecl = g_cgNHandlerDecl = 0;
    g_cgChainOK = FALSE; g_offObjClass = g_offSuper = -1;
    if (!g_objArray || IsBadReadPtr(g_objArray, sizeof(TArrayLite))) return;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return;
    void *clsFunction = NULL, *clsStructProp = NULL, *clsByteProp = NULL,
         *clsController = NULL, *clsPlayerCtrl = NULL, *clsFloatProp = NULL;
    void *instLoc = NULL, *instTrig = NULL, *instPhys = NULL, *instGS = NULL;
    DWORD t0 = GetTickCount();
    char buf[300];
    for (int i = 0; i < n; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        objName(o, buf, sizeof(buf));
        char *sp = strchr(buf, ' ');
        if (!sp) continue;
        *sp = 0;
        const char *tok = buf, *path = sp + 1;
        if (!strcmp(tok, "Class")) {
            if (g_cgNKCls < CG_MAX_KCLS) g_cgKCls[g_cgNKCls++] = o;
            if      (!strcmp(path, "Engine.Actor"))            g_clsActor = o;
            else if (!strcmp(path, "Engine.Pawn"))             g_clsPawn = o;
            else if (!strcmp(path, "Engine.Controller"))       clsController = o;
            else if (!strcmp(path, "Engine.PlayerController")) clsPlayerCtrl = o;
            else if (!strcmp(path, "Engine.Trigger"))          g_clsTrigger = o;
            else if (!strcmp(path, "Engine.Projectile"))       g_clsProjectile = o;
            else if (!_stricmp(path, "hgame.HPCharacter"))     g_clsHPChar = o;
            else if (!strcmp(path, "Core.Function"))           clsFunction = o;
            else if (!strcmp(path, "Core.StructProperty"))     clsStructProp = o;
            else if (!strcmp(path, "Core.ByteProperty"))       clsByteProp = o;
            else if (!strcmp(path, "Core.FloatProperty"))      clsFloatProp = o;
            continue;
        }
        int dots = 0;
        for (const char *c = path; *c; c++) if (*c == '.') dots++;
        BOOL isFn = !strcmp(tok, "Function");
        // Pkg.Class.Member, plus Pkg.Class.State.Handler for spell handlers
        // (HP2's SpongifyPad keeps HandleSpellSpongify inside a state).
        if (dots != 2 && !(dots == 3 && isFn)) continue;
        const char *name = cgLastComp(path);
        int kind = 0;
        if (!strcmp(tok, "ClassProperty")) {
            if (ciHas(name, "vulnerable")) kind = CGK_VULNCLS;
            else if (ciHas(name, "spell") && (ciHas(name, "react") || ciHas(name, "activ") ||
                     ciHas(name, "require") || ciHas(name, "need") || ciHas(name, "trigger") ||
                     ciHas(name, "accept") || ciHas(name, "open") || ciHas(name, "unlock")))
                kind = CGK_SPELLCLS;
        } else if (!strcmp(tok, "ByteProperty")) {
            if (ciHas(name, "vulnerable")) kind = CGK_VULNBYTE;
            else if (!strcmp(path, "Engine.Actor.Physics")) instPhys = o;
        } else if (!strcmp(tok, "FloatProperty")) {
            if (!_stricmp(name, "SizeModifier")) kind = CGK_SIZE;
            else if (!strcmp(path, "Engine.Pawn.GroundSpeed")) instGS = o;
        } else if (!strcmp(tok, "StructProperty")) {
            if (!_stricmp(name, "CentreOffset") || !_stricmp(name, "CenterOffset"))
                kind = CGK_CENTRE;
            else if (!strcmp(path, "Engine.Actor.Location")) instLoc = o;
        } else if (isFn) {
            if (!strcmp(path, "Engine.Actor.Trigger")) instTrig = o;
            if (!_strnicmp(name, "HandleSpell", 11) || !_stricmp(name, "OnHandleSpell") ||
                !_stricmp(name, "OnSpellHit") || !_stricmp(name, "SpellHit") ||
                !_strnicmp(name, "HitBySpell", 10) || !_strnicmp(name, "OnHitBySpell", 12))
                kind = CGK_FN_HANDLE;
            else if (dots == 3)                       continue; // class-level only below
            else if (!_stricmp(name, "ProcessTouch")) kind = CGK_FN_PTOUCH;
            else if (!_stricmp(name, "Touch"))        kind = CGK_FN_TOUCH;
            else if (!_stricmp(name, "Trigger"))      kind = CGK_FN_TRIGGER;
        }
        if (!kind || g_cgNIdx >= CG_MAX_IDX) continue;
        CgIdx *e = &g_cgIdx[g_cgNIdx];
        e->obj = o; e->kind = kind; e->off = -1; e->stateFn = (dots == 3);
        if (kind <= CGK_CENTRE) {
            if (g_propOffsetField < 0 || IsBadReadPtr(o, g_propOffsetField + 4)) continue;
            e->off = (int)*(DWORD *)((BYTE *)o + g_propOffsetField);
            if (e->off < 0 || e->off > 0x4000) continue;
        }
        // decl = Pkg.Class (cut at the second dot)
        const char *d1 = strchr(path, '.');
        const char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
        int dl = d2 ? (int)(d2 - path) : 0;
        if (dl <= 0 || dl >= (int)sizeof(e->decl)) continue;
        memcpy(e->decl, path, dl); e->decl[dl] = 0;
        e->declHash = cgHash(e->decl);
        strncpy(e->name, name, sizeof(e->name) - 1); e->name[sizeof(e->name) - 1] = 0;
        if (kind == CGK_VULNCLS || kind == CGK_VULNBYTE) g_cgNVulnDecl++;
        if (kind == CGK_FN_HANDLE) g_cgNHandlerDecl++;
        g_cgNIdx++;
    }
    qsort(g_cgKCls, g_cgNKCls, sizeof(void *), cgKClsCmp);
    if (g_cgNKCls >= CG_MAX_KCLS)
        logf_("  [castgame] WARNING: class set capped at %d - some actors will be ignored", CG_MAX_KCLS);

    // UObject::Class - the pointer inside an instance that names its class.
    {
        void *inst[4] = { instLoc, instTrig, instPhys, instGS };
        void *want[4] = { clsStructProp, clsFunction, clsByteProp, clsFloatProp };
        g_offObjClass = cgCalibratePtrField(inst, want, 4, 0x60, "UObject::Class");
    }
    // UStruct::SuperField - the parent class pointer inside a UClass.
    {
        void *inst[3] = { g_clsPawn, clsPlayerCtrl, g_clsProjectile };
        void *want[3] = { g_clsActor, clsController, g_clsActor };
        g_offSuper = cgCalibratePtrField(inst, want, 3, 0xE0, "UStruct::SuperField");
    }
    // Prove the chain on a pawn we know: its class must reach Engine.Pawn
    // and Engine.Actor, or the whole chain feature is disabled (name-only
    // fallback) instead of trusting a wrong dword.
    if (g_offObjClass >= 0 && g_offSuper >= 0 && g_clsActor && g_clsPawn) {
        void *probe = g_pawn[1] ? g_pawn[1] : (g_pawn[0] ? g_pawn[0] : findActorByClass("harry"));
        void *c = cgClassOf(probe);
        BOOL sawPawn = FALSE, sawActor = FALSE; int depth = 0;
        while (c && depth++ < 32) {
            if (c == g_clsPawn) sawPawn = TRUE;
            if (c == g_clsActor) sawActor = TRUE;
            c = cgSuper(c);
        }
        g_cgChainOK = sawPawn && sawActor;
        char cb[160];
        logf_("  [castgame] class chain %s (probe %s: depth=%d pawn=%d actor=%d)",
              g_cgChainOK ? "VERIFIED" : "FAILED - name-only fallback",
              probe ? objName(probe, cb, sizeof(cb)) : "<none>", depth, sawPawn, sawActor);
    } else {
        logf_("  [castgame] class chain unavailable (Class=+0x%X Super=+0x%X "
              "Actor=%p Pawn=%p) - name-only fallback", g_offObjClass, g_offSuper,
              g_clsActor, g_clsPawn);
    }
    int nv = 0, nb = 0, ns = 0, nh = 0, npt = 0;
    for (int k = 0; k < g_cgNIdx; k++) {
        switch (g_cgIdx[k].kind) {
            case CGK_VULNCLS: nv++; break;   case CGK_VULNBYTE: nb++; break;
            case CGK_SPELLCLS: ns++; break;  case CGK_FN_HANDLE: nh++; break;
            case CGK_FN_PTOUCH: npt++; break; default: break;
        }
    }
    logf_("  [castgame] index: %d classes, %d entries (vulnerableToClass-like=%d "
          "vulnerable bytes=%d spell ClassProps=%d spell handlers=%d ProcessTouch=%d) "
          "in %lums", g_cgNKCls, g_cgNIdx, nv, nb, ns, nh, npt,
          (unsigned long)(GetTickCount() - t0));
    int shown = 0;
    for (int k = 0; k < g_cgNIdx && shown < 14; k++) {
        CgIdx *e = &g_cgIdx[k];
        if (e->kind == CGK_VULNCLS || e->kind == CGK_VULNBYTE || e->kind == CGK_FN_PTOUCH) {
            logf_("      %s.%s (+0x%X)", e->decl, e->name, e->off);
            shown++;
        }
    }
    shown = 0;
    for (int k = 0; k < g_cgNIdx && shown < 24; k++) {
        CgIdx *e = &g_cgIdx[k];
        if (e->kind == CGK_FN_HANDLE) { logf_("      handler %s.%s", e->decl, e->name); shown++; }
    }
}

// Everything the cast path needs to know about one class, resolved by walking
// its class chain against the index (most-derived declaration wins, so the
// function object we later hand to ProcessEvent is the real override).
static CgClass *cgClassInfo(void *cls)
{
    if (!cls) return NULL;
    for (int k = 0; k < g_cgNCls; k++) if (g_cgCls[k].cls == cls) return &g_cgCls[k];
    if (g_cgNCls >= CG_MAX_CLS) {
        static BOOL sWarned = FALSE;
        if (!sWarned) { sWarned = TRUE; logf_("  [castgame] class cache full (%d) - later classes ignored", CG_MAX_CLS); }
        return NULL;
    }
    CgClass *ci = &g_cgCls[g_cgNCls++];
    memset(ci, 0, sizeof(*ci));
    ci->cls = cls;
    ci->vulnOff = ci->vulnByteOff = ci->spellClsOff = ci->sizeOff = ci->centreOff = -1;
    char buf[300];
    objName(cls, buf, sizeof(buf));
    const char *sp = strchr(buf, ' ');
    if (sp) {
        strncpy(ci->path, sp + 1, sizeof(ci->path) - 1);
        strncpy(ci->token, cgLastComp(sp + 1), sizeof(ci->token) - 1);
    }
    void *c = cls; int depth = 0;
    while (c && depth++ < 32) {
        objName(c, buf, sizeof(buf));
        const char *csp = strchr(buf, ' ');
        const char *cpath = csp ? csp + 1 : buf;
        const char *ctok = cgLastComp(cpath);
        if (c == g_clsActor)      ci->isActor = TRUE;
        if (c == g_clsPawn)       ci->isPawn = TRUE;
        if (c == g_clsHPChar)     ci->isHero = TRUE;
        if (c == g_clsTrigger)    ci->isTrigger = TRUE;
        if (c == g_clsProjectile) ci->isProjectile = TRUE;
        if (ciHas(ctok, "SpellTrigger")) ci->nameSpellTrigger = TRUE;
        if (ciHas(ctok, "Trigger"))      ci->nameTrigger = TRUE;
        DWORD ph = cgHash(cpath);
        for (int k = 0; k < g_cgNIdx; k++) {
            CgIdx *e = &g_cgIdx[k];
            if (e->declHash != ph || strcmp(e->decl, cpath)) continue;
            switch (e->kind) {
            case CGK_VULNCLS:
                if (ci->vulnOff < 0) { ci->vulnOff = e->off;
                    strncpy(ci->vulnName, e->name, sizeof(ci->vulnName) - 1); }
                break;
            case CGK_VULNBYTE: if (ci->vulnByteOff < 0) ci->vulnByteOff = e->off; break;
            case CGK_SPELLCLS: if (ci->spellClsOff < 0) ci->spellClsOff = e->off; break;
            case CGK_SIZE:     if (ci->sizeOff < 0)     ci->sizeOff = e->off; break;
            case CGK_CENTRE:   if (ci->centreOff < 0)   ci->centreOff = e->off; break;
            case CGK_FN_PTOUCH:  if (!ci->fnPTouch) { ci->fnPTouch = e->obj;
                                     strncpy(ci->pTouchDecl, e->decl, 79); } break;
            case CGK_FN_TOUCH:   if (!ci->fnTouch) { ci->fnTouch = e->obj;
                                     strncpy(ci->touchDecl, e->decl, 79); } break;
            case CGK_FN_TRIGGER: if (!ci->fnTrigger) ci->fnTrigger = e->obj; break;
            case CGK_FN_HANDLE: {
                BOOL spec = (_strnicmp(e->name, "HandleSpell", 11) == 0 && strlen(e->name) > 11);
                if (spec) {
                    int dup = -1;
                    for (int s = 0; s < ci->nSpec; s++)
                        if (!_stricmp(ci->specName[s], e->name)) { dup = s; break; }
                    if (dup >= 0) {
                        // same class, class-level declaration beats a state's
                        if (!e->stateFn && ci->specState[dup] && !strcmp(ci->specDecl[dup], e->decl)) {
                            ci->fnSpec[dup] = e->obj; ci->specState[dup] = FALSE;
                        }
                    } else if (ci->nSpec < 8) {
                        ci->fnSpec[ci->nSpec] = e->obj;
                        ci->specState[ci->nSpec] = e->stateFn;
                        strncpy(ci->specName[ci->nSpec], e->name, 39);
                        strncpy(ci->specDecl[ci->nSpec], e->decl, 79);
                        ci->nSpec++;
                    }
                } else {
                    int dup = -1;
                    for (int s = 0; s < ci->nGen; s++)
                        if (!_stricmp(ci->genName[s], e->name)) { dup = s; break; }
                    if (dup >= 0) {
                        if (!e->stateFn && ci->genState[dup] && !strcmp(ci->genDecl[dup], e->decl)) {
                            ci->fnGen[dup] = e->obj; ci->genState[dup] = FALSE;
                        }
                    } else if (ci->nGen < 4) {
                        ci->fnGen[ci->nGen] = e->obj;
                        ci->genState[ci->nGen] = e->stateFn;
                        strncpy(ci->genName[ci->nGen], e->name, 39);
                        strncpy(ci->genDecl[ci->nGen], e->decl, 79);
                        ci->nGen++;
                    }
                }
                break; }
            default: break;
            }
        }
        c = cgSuper(c);
    }
    return ci;
}

// Handler declarations on the shared bases say nothing about ONE object:
// HP2's HPawn declares every HandleSpell<Name> as a stub and the real work is
// in the subclass override. Only handlers declared below these count as
// "this object reacts to that spell".
static BOOL cgSharedBaseDecl(const char *decl)
{
    if (!decl) return TRUE;
    if (!_strnicmp(decl, "Engine.", 7) || !_strnicmp(decl, "Core.", 5)) return TRUE;
    static const char *shared[] = { "hgame.HPPawn", "hgame.HPCharacter", "hgame.HPHeroPawn",
        "hgame.HPProp", "KWGame.KWPawn", "KWGame.KWPawnNative", "hgame.HPHeroController",
        "hgame.HPAIController", NULL };
    for (int k = 0; shared[k]; k++) if (!_stricmp(decl, shared[k])) return TRUE;
    return FALSE;
}
static int cgOwnSpecHandlers(CgClass *ci)
{
    int n = 0;
    if (!ci) return 0;
    for (int s = 0; s < ci->nSpec; s++) if (!cgSharedBaseDecl(ci->specDecl[s])) n++;
    return n;
}

// A Class object by its last path component (case-insensitive), e.g.
// "LapiforsSpell" -> Class hgame.LapiforsSpell. Known-class set only.
static void *cgFindClassByToken(const char *tok)
{
    if (!tok || !tok[0]) return NULL;
    char buf[200];
    for (int k = 0; k < g_cgNKCls; k++) {
        objName(g_cgKCls[k], buf, sizeof(buf));
        const char *sp = strchr(buf, ' ');
        if (!sp) continue;
        if (!_stricmp(cgLastComp(sp + 1), tok)) return g_cgKCls[k];
    }
    return NULL;
}

// Statues and pads name the spell they react to in their handler:
// HandleSpellLapifors -> hgame.LapiforsSpell (or spellLapifors). This is the
// spell player 1's wand ends up choosing for that object; use it whenever the
// object does not carry an explicit spell class.
static void cgResolveHandlerSpell(CgClass *ci)
{
    if (!ci || ci->handlerSpell || !ci->nSpec || ci->handlerSpellTried) return;
    ci->handlerSpellTried = TRUE;
    for (int s = 0; s < ci->nSpec && !ci->handlerSpell; s++) {
        if (cgSharedBaseDecl(ci->specDecl[s])) continue;   // stubs prove nothing
        const char *tok = ci->specName[s] + 11;
        if (!tok[0]) continue;
        char cand[64];
        _snprintf(cand, sizeof(cand) - 1, "%sSpell", tok); cand[sizeof(cand) - 1] = 0;
        void *c = cgFindClassByToken(cand);
        if (!c) { _snprintf(cand, sizeof(cand) - 1, "spell%s", tok); cand[sizeof(cand) - 1] = 0;
                  c = cgFindClassByToken(cand); }
        if (!c) c = cgFindClassByToken(tok);
        if (c) { ci->handlerSpell = c; strncpy(ci->handlerSpellFrom, ci->specName[s], 39); }
    }
}

// Abstract/generic classes must not be cast: baseSpell / Projectile as a
// vulnerableTo value means "any spell", not a class to spawn.
static BOOL cgGenericSpellClass(void *cls)
{
    if (!cls) return TRUE;
    char buf[200]; objName(cls, buf, sizeof(buf));
    const char *sp = strchr(buf, ' ');
    const char *tok = cgLastComp(sp ? sp + 1 : buf);
    return !_stricmp(tok, "baseSpell") || !_stricmp(tok, "Projectile") ||
           !_stricmp(tok, "Actor") || !_stricmp(tok, "Spell");
}

static BOOL cgIsPlayerPawn(void *o)
{
    for (int p = 0; p < 8; p++) if (g_pawn[p] && g_pawn[p] == o) return TRUE;
    return FALSE;
}

static BOOL cgDeleted(void *o)
{
    if (g_offDeleteMe <= 0 || !g_maskDeleteMe || IsBadReadPtr((BYTE *)o + (g_offDeleteMe & ~3), 4))
        return FALSE;
    return (*(DWORD *)((BYTE *)o + (g_offDeleteMe & ~3)) & g_maskDeleteMe) != 0;
}

// v50's name heuristic, kept only for the no-chain fallback.
static BOOL cgNameLooksCastable(const char *full)
{
    if (ciHas(full, "SpellTrigger")) return TRUE;
    if (ciHas(full, "Cast") && ciHas(full, "Trigger")) return TRUE;
    static const char *hint[] = { "Statue", "SpongifyPad", "JumpPad", "BouncePad",
        "Spongify", "Lapifors", "Draconifors", "Carpe", "Depulso", "Flipendo",
        "Glacius", "Alohomora", "Rictusempra", NULL };
    for (int k = 0; hint[k]; k++) if (ciHas(full, hint[k])) return TRUE;
    return FALSE;
}

// The level's castable actors. Rebuilt on demand (aim start) and at most
// every 4 s while aiming; the per-frame ray test only walks this list.
static void cgScanCandidates(void *self, BOOL force)
{
    DWORD now = GetTickCount();
    if (!force && g_cgScanAt && now - g_cgScanAt < 6000) return;
    cgBuildIndex();
    g_cgScanAt = now;
    int prevN = g_cgNCand;
    g_cgNCand = 0;
    if (!g_objArray || F.Location <= 0) return;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return;
    int examined = 0, actors = 0;
    char buf[300];
    for (int j = 0; j < n && g_cgNCand < CG_MAX_CAND; j++) {
        void *o = g_objArray->Data[j];
        if (!o || o == self || IsBadReadPtr(o, 0x100) || cgIsPlayerPawn(o) || !actorInCurrentLevel(o)) continue;
        objName(o, buf, sizeof(buf));
        if (isReflection(buf)) continue;
        const char *sp = strchr(buf, ' ');
        if (!sp || !strchr(sp + 1, '.')) continue;
        examined++;
        int kind = CGC_NONE; void *spellCls = NULL; CgClass *info = NULL;
        if (g_cgChainOK) {
            void *cls = cgClassOf(o);
            info = cgClassInfo(cls);
            if (!info || !info->isActor || info->isProjectile) continue;
            actors++;
            // never the playable trio (any instance), spell effects, cursors
            BOOL trio = FALSE;
            for (int t = 0; t < 8 && kCharClass[t]; t++)
                if (!_stricmp(info->token, kCharClass[t])) { trio = TRUE; break; }
            if (trio || ciHas(info->token, "Cursor") || ciHas(info->token, "Emitter")) continue;
            if (IsBadReadPtr(o, F.Location + 12) || cgDeleted(o)) continue;
            if (info->vulnOff > 0 && !IsBadReadPtr((BYTE *)o + info->vulnOff, 4)) {
                void *v = *(void **)((BYTE *)o + info->vulnOff);
                if (cgIsKnownClass(v)) { kind = CGC_VULN; spellCls = v; }
            }
            if (!kind && info->vulnByteOff > 0 && !IsBadReadPtr((BYTE *)o + info->vulnByteOff, 1) &&
                *((BYTE *)o + info->vulnByteOff) != 0)
                kind = CGC_VULNBYTE;
            if (!kind && info->nameSpellTrigger) kind = CGC_TRIG;
            // Objects that override a specific HandleSpell<Name> below the
            // shared bases react to that spell even when their vulnerable
            // property is unset (a statue's own Lapifors handler, a pad's
            // Spongify). The generic OnSpellHit stubs do not count.
            //
            // Handler-only admission is judged per object, not per level.
            // The v54 gate required the whole level to contain zero
            // vulnerable-typed classes, so one unrelated statue/pad with a
            // live vulnerableTo class silently suppressed this classification
            // for EVERY otherwise-unnamed object in the same map - including
            // cooperative triggers that only carry their own spell handler.
            // Whether some *other* class declares a vulnerable property says
            // nothing about this object's own dispatch, so only the class
            // chain of the object itself (own spec handlers below the shared
            // bases) decides here.
            if (!kind) {
                int own = cgOwnSpecHandlers(info);
                if (own > 0 && own <= 2) kind = CGC_FN;
            }
            if (kind && (!spellCls || cgGenericSpellClass(spellCls)) && info->spellClsOff > 0 &&
                !IsBadReadPtr((BYTE *)o + info->spellClsOff, 4)) {
                void *v = *(void **)((BYTE *)o + info->spellClsOff);
                if (cgIsKnownClass(v) && !cgGenericSpellClass(v)) spellCls = v;
            }
            if (kind && (!spellCls || cgGenericSpellClass(spellCls)) && info->nSpec) {
                cgResolveHandlerSpell(info);
                if (info->handlerSpell) spellCls = info->handlerSpell;
            }
            if (spellCls && cgGenericSpellClass(spellCls)) spellCls = NULL;
        } else {
            if (!cgNameLooksCastable(buf) || IsBadReadPtr(o, F.Location + 12)) continue;
            kind = CGC_NAME;
        }
        if (!kind) continue;
        CgCand *c = &g_cgCand[g_cgNCand++];
        c->obj = o; c->slot = j; c->info = info; c->spellCls = spellCls; c->kind = kind;
        strncpy(c->name, buf, sizeof(c->name) - 1); c->name[sizeof(c->name) - 1] = 0;
        memcpy(c->loc, (BYTE *)o + F.Location, 12);
    }
    if (!g_cgScanLogged || force || g_cgNCand != prevN) {
        BOOL firstTime = !g_cgScanLogged;
        g_cgScanLogged = TRUE;
        float *pl = (self && !IsBadReadPtr(self, F.Location + 12))
                  ? (float *)((BYTE *)self + F.Location) : NULL;
        logf_("  [castgame-scan] %d castable actors (%d objects, %d actors examined, "
              "chain=%d vulnDecl=%d handlerDecl=%d)%s", g_cgNCand, examined, actors,
              g_cgChainOK, g_cgNVulnDecl, g_cgNHandlerDecl,
              firstTime ? "" : " (refresh)");
        if (firstTime || g_cgNCand <= 24) {
            char cb[160];
            for (int k = 0; k < g_cgNCand && k < 40; k++) {
                CgCand *c = &g_cgCand[k];
                float d = -1.0f;
                if (pl) { float dx = c->loc[0]-pl[0], dy = c->loc[1]-pl[1], dz = c->loc[2]-pl[2];
                          d = sqrtf(dx*dx + dy*dy + dz*dz); }
                const char *kn = c->kind == CGC_VULN ? "vulnerableTo" :
                                 c->kind == CGC_VULNBYTE ? "vulnerable-byte" :
                                 c->kind == CGC_TRIG ? "spelltrigger" :
                                 c->kind == CGC_FN ? "handler-only" : "name";
                logf_("      %s  [%s %s] spell=%s%s%s dist=%.0f handlers=%d/%d touch=%d",
                      c->name, kn, c->info ? c->info->vulnName : "",
                      c->spellCls ? objName(c->spellCls, cb, sizeof(cb)) : "-",
                      (c->info && c->spellCls && c->spellCls == c->info->handlerSpell) ? " via " : "",
                      (c->info && c->spellCls && c->spellCls == c->info->handlerSpell) ? c->info->handlerSpellFrom : "",
                      d, cgOwnSpecHandlers(c->info), c->info ? c->info->nGen : 0,
                      c->info && c->info->fnTouch ? 1 : 0);
            }
        }
        if (g_cgNCand == 0)
            logf_("  [castgame-scan] NOTHING castable found: %s", g_cgChainOK
                  ? (g_cgNVulnDecl ? "no actor here has a live vulnerable class - "
                                     "cast near a statue/pad and check again"
                                   : "this build has no vulnerable* property; send this log")
                  : "class-chain calibration failed; send this log");
    }
}

static CgCand *cgFindCand(void *obj)
{
    if (!obj) return NULL;
    for (int k = 0; k < g_cgNCand; k++) if (g_cgCand[k].obj == obj) return &g_cgCand[k];
    return NULL;
}

static BOOL cgCandAlive(CgCand *c)
{
    if (!c || !c->obj || !g_objArray || c->slot < 0 || c->slot >= g_objArray->Num) return FALSE;
    if (g_objArray->Data[c->slot] != c->obj || IsBadReadPtr(c->obj, F.Location + 12)) return FALSE;
    // Same slot, same class pointer = same object (slots are reused only
    // after GC, which also invalidates the class pointer we cached).
    if (c->info && cgClassOf(c->obj) != c->info->cls) return FALSE;
    return !cgDeleted(c->obj) && actorInCurrentLevel(c->obj);
}

// The target's aim point: Location + CentreOffset (the game's own targeting
// centre), else a little above the actor origin.
static void cgAimPoint(CgCand *c, float out[3])
{
    float *l = (float *)((BYTE *)c->obj + F.Location);
    out[0] = l[0]; out[1] = l[1]; out[2] = l[2];
    static int sOffCH = -2;
    if (sOffCH == -2) sOffCH = propOffset("Engine.Actor.CollisionHeight");
    BOOL haveCentre = FALSE;
    if (c->info && c->info->centreOff > 0 && !IsBadReadPtr((BYTE *)c->obj + c->info->centreOff, 12)) {
        float *co = (float *)((BYTE *)c->obj + c->info->centreOff);
        if (fabsf(co[0]) < 500.0f && fabsf(co[1]) < 500.0f && fabsf(co[2]) < 500.0f) {
            float rotated[3];
            if (g_nativeAim && F.Rotation > 0 && !IsBadReadPtr((BYTE *)c->obj + F.Rotation,12)) {
                hp3aim::rotate(co,(int *)((BYTE *)c->obj+F.Rotation),rotated); co=rotated;
            }
            out[0] += co[0]; out[1] += co[1]; out[2] += co[2]; haveCentre = TRUE;
        }
    }
    if (!haveCentre && sOffCH > 0 && !IsBadReadPtr((BYTE *)c->obj + sOffCH, 4)) {
        float h = *(float *)((BYTE *)c->obj + sOffCH);
        if (h > 1.0f && h < 1000.0f) out[2] += h * 0.35f;
    }
}

static float cgHitRadius(CgCand *c)
{
    static int sOffCR = -2;
    if (sOffCR == -2) sOffCR = propOffset("Engine.Actor.CollisionRadius");
    float r = 60.0f;
    if (sOffCR > 0 && !IsBadReadPtr((BYTE *)c->obj + sOffCR, 4)) {
        float cr = *(float *)((BYTE *)c->obj + sOffCR);
        if (cr > 1.0f && cr < 1000.0f) r = cr;
    }
    return r;
}

// Player-1-style target pick: the castable actor under this camera's spell
// line (3D corridor from the real spell origin, LOS-checked). SizeModifier
// widens/narrows the acceptance exactly as the cursor's does.
static CgCand *cgPickTarget(int i, void *pawn, float *outDist, BOOL verbose, float aimOut[3])
{
    if (!g_castGameplay || !pawn || F.Location <= 0 || IsBadReadPtr(pawn, F.Location + 12))
        return NULL;
    cgScanCandidates(pawn, FALSE);
    int cy = playerCamYaw(i), cp = playerCamPitch(i);
    double ry = cy * (6.283185307179586 / 65536.0);
    double rp = cp * (6.283185307179586 / 65536.0);
    float cpr = (float)cos(rp);
    float dir[3] = { (float)cos(ry) * cpr, (float)sin(ry) * cpr, (float)sin(rp) };
    float *pl = (float *)((BYTE *)pawn + F.Location);
    float org[3] = { pl[0] + dir[0] * 90.0f, pl[1] + dir[1] * 90.0f, pl[2] + dir[2] * 90.0f + 45.0f };

    float maxT = 3200.0f;
    if (g_nativeAim) {
        maxT = nativeAimRange() - 90.0f;
        if (i > 0 && i < 8 && g_aimViewValid[i]) {
            memcpy(org,g_aimViewLoc[i],12);
            hp3aim::direction(g_aimViewRot[i],dir);
            maxT=hp3aim::rayLength(org,pl,dir,nativeAimRange());
        }
    }
    CgCand *best = NULL; float bestScore = 1.0e30f, bestT = 0, bestPerp = 0, bestAim[3] = {0,0,0};
    CgCand *nearest = NULL; float nearestD = 1.0e30f;
    for (int k = 0; k < g_cgNCand; k++) {
        CgCand *c = &g_cgCand[k];
        if (!cgCandAlive(c)) continue;
        if (g_nativeAim) {
            static int liveVuln = -1;
            if (liveVuln < 0) liveVuln=propOffset("Engine.Actor.vulnerableToClass");
            if (liveVuln < 0 || IsBadReadPtr((BYTE *)c->obj+liveVuln,4)) continue;
            void *v=*(void **)((BYTE *)c->obj+liveVuln);
            if (!cgIsKnownClass(v) || cgGenericSpellClass(v)) continue;
            c->spellCls=v;
            static int projOff=-1, collideOff=-1;
            static DWORD projMask=0,collideMask=0;
            if(projOff<0) {projOff=propOffset("Engine.Actor.bProjTarget");projMask=nativeBoolBitMask("Engine.Actor.bProjTarget");}
            if(collideOff<0) {collideOff=propOffset("Engine.Actor.bCollideActors");collideMask=nativeBoolBitMask("Engine.Actor.bCollideActors");}
            if(projOff>0 && projMask && !IsBadReadPtr((BYTE *)c->obj+(projOff&~3),4) &&
               !(*(DWORD *)((BYTE *)c->obj+(projOff&~3)) & projMask)) continue;
            if(collideOff>0 && collideMask && !IsBadReadPtr((BYTE *)c->obj+(collideOff&~3),4) &&
               !(*(DWORD *)((BYTE *)c->obj+(collideOff&~3)) & collideMask)) continue;
        }
        float tl[3]; cgAimPoint(c, tl);
        float radius = cgHitRadius(c) + 90.0f;
        if (c->info && c->info->sizeOff > 0 && !IsBadReadPtr((BYTE *)c->obj + c->info->sizeOff, 4)) {
            float sm = *(float *)((BYTE *)c->obj + c->info->sizeOff);
            if (sm >= 0.3f && sm <= 4.0f) radius *= sm;
        }
        if (radius < 140.0f) radius = 140.0f;
        float vx = tl[0] - org[0], vy = tl[1] - org[1], vz = tl[2] - org[2];
        float dist = sqrtf(vx * vx + vy * vy + vz * vz);
        if (dist < nearestD) { nearestD = dist; nearest = c; }
        float t = vx * dir[0] + vy * dir[1] + vz * dir[2];
        if (t < 40.0f || t > maxT) continue;
        float perp2 = dist * dist - t * t; if (perp2 < 0.0f) perp2 = 0.0f;
        float perp = sqrtf(perp2);
        if (perp > radius) continue;
        // LOS to the aim point, or to a point pulled back in front of the
        // object (a solid mover's own surface must not veto itself).
        if (!fastTraceClear(pawn, org, tl)) {
            float back = cgHitRadius(c) + 10.0f;
            float q[3] = { tl[0] - dir[0] * back, tl[1] - dir[1] * back, tl[2] - dir[2] * back };
            if (!fastTraceClear(pawn, org, q)) continue;
        }
        float score = t + perp * 3.0f;
        if (score < bestScore) { bestScore = score; best = c; bestT = t; bestPerp = perp;
                                 memcpy(bestAim, tl, 12); }
    }
    if (outDist) *outDist = bestT;
    if (best && aimOut) memcpy(aimOut, bestAim, 12);
    if (verbose) {
        char cb[160];
        if (best)
            logf_("    [castgame] p%d target -> %s ray=%.0f perp=%.0f spell=%s", i,
                  best->name, bestT, bestPerp,
                  best->spellCls ? objName(best->spellCls, cb, sizeof(cb)) : "(none on object)");
        else if (nearest)
            logf_("    [castgame] p%d no target on the spell line (%d castable; nearest %s at %.0f)",
                  i, g_cgNCand, nearest->name, nearestD);
        else
            logf_("    [castgame] p%d no castable actors in this level (see [castgame-scan])", i);
    }
    return best;
}

// Parameter layout of a script function, from its child *Property objects
// (offset = UProperty::Offset). Cached per function; logged once so the
// hardware log shows the signature we filled.
static CgFnLayout *cgFnLayout(void *fn)
{
    if (!fn) return NULL;
    for (int k = 0; k < g_cgNLay; k++) if (g_cgLay[k].fn == fn) return &g_cgLay[k];
    if (g_cgNLay >= 24 || !g_objArray || g_propOffsetField < 0) return NULL;
    CgFnLayout *L = &g_cgLay[g_cgNLay++];
    memset(L, 0, sizeof(*L));
    L->fn = fn;
    char fpath[200];
    if (!functionPath(fn, fpath, sizeof(fpath))) return L;
    size_t plen = strlen(fpath);
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return L;
    char buf[300];
    for (int j = 0; j < n; j++) {
        void *p = g_objArray->Data[j];
        if (!p || IsBadReadPtr(p, g_propOffsetField + 4)) continue;
        objName(p, buf, sizeof(buf));
        char *sp = strchr(buf, ' ');
        if (!sp) continue;
        const char *path = sp + 1;
        if (strncmp(path, fpath, plen) || path[plen] != '.') continue;
        if (strchr(path + plen + 1, '.')) continue;
        *sp = 0;
        int kind = 0, size = 4;
        if      (!strcmp(buf, "ClassProperty"))  kind = 1;
        else if (!strcmp(buf, "ObjectProperty")) kind = 2;
        else if (!strcmp(buf, "StructProperty")) { kind = 3; size = 12; }
        else if (!strcmp(buf, "FloatProperty"))  kind = 4;
        else if (!strcmp(buf, "BoolProperty"))   kind = 5;
        else if (!strcmp(buf, "ByteProperty"))   { kind = 6; size = 1; }
        else if (!strcmp(buf, "IntProperty") || !strcmp(buf, "NameProperty")) kind = 7;
        // A safety-sensitive caller must not treat a type we do not model as
        // absent. Keep recording supported properties for ordinary cgCall,
        // but latch the layout as unsuitable for ABI-certified bridges.
        else { L->unknown = TRUE; continue; }
        int off = (int)*(DWORD *)((BYTE *)p + g_propOffsetField);
        if (off < 0 || off + size > 240) { L->unknown = TRUE; continue; }
        if (L->n >= (int)(sizeof(L->p) / sizeof(L->p[0]))) {
            L->unknown = TRUE;
            continue;
        }
        L->p[L->n].off = off; L->p[L->n].kind = kind;
        strncpy(L->p[L->n].name, path + plen + 1, 31);
        L->p[L->n].name[31] = 0;
        L->n++;
        if (off + size > L->total) L->total = off + size;
    }
    L->complete = TRUE;
    char line[400]; int c = _snprintf(line, sizeof(line) - 1, "  [castgame-fn] %s:", fpath);
    for (int k = 0; k < L->n && c < (int)sizeof(line) - 40; k++)
        c += _snprintf(line + c, sizeof(line) - 1 - c, " +0x%02X %s", L->p[k].off, L->p[k].name);
    line[sizeof(line) - 1] = 0;
    logf_("%s%s%s", line, L->n ? "" : " (no params)",
          L->unknown ? " [unsupported/incomplete property]" : "");
    return L;
}

// Call a spell-related script function with parameters filled by NAME/TYPE:
//   Class     -> the spell class
//   Object    -> spell/projectile -> spell actor; instigator/caster/owner/
//                pawn/source -> caster; anything else -> "the other actor"
//                (the target when self is the spell, the spell when self is
//                the target) - matches ProcessTouch(Other, HitLocation),
//                HandleSpellX(spell, vHitLocation), Touch(Other),
//                Trigger(Other, EventInstigator)
//   Struct    -> hit location (offset/normal params stay zero)
//   Float     -> charge = 1.0
static void cgCall(int i, void *self, void *fn, BOOL selfIsSpell, void *spell,
                   void *target, void *caster, void *spellCls, const float hit[3],
                   const char *why)
{
    if (!self || !fn || !g_ProcessEvent) return;
    BYTE parms[256]; memset(parms, 0, sizeof(parms));
    CgFnLayout *L = cgFnLayout(fn);
    void *other = selfIsSpell ? target : spell;
    if (L) {
        for (int k = 0; k < L->n; k++) {
            int off = L->p[k].off; const char *nm = L->p[k].name;
            if (!_stricmp(nm, "ReturnValue")) continue;
            switch (L->p[k].kind) {
            case 1: *(void **)(parms + off) = spellCls; break;
            case 2:
                if (ciHas(nm, "spell") || ciHas(nm, "projectile") || ciHas(nm, "missile"))
                    *(void **)(parms + off) = spell;
                else if (ciHas(nm, "instigator") || ciHas(nm, "caster") || ciHas(nm, "owner") ||
                         ciHas(nm, "pawn") || ciHas(nm, "source"))
                    *(void **)(parms + off) = caster;
                else
                    *(void **)(parms + off) = other;
                break;
            case 3:
                if (ciHas(nm, "loc") || ciHas(nm, "hit") || ciHas(nm, "pos"))
                    memcpy(parms + off, hit, 12);
                break;
            case 4: if (ciHas(nm, "charge")) *(float *)(parms + off) = 1.0f; break;
            default: break;
            }
        }
    } else {
        // No reflection for this function: conventional (Actor, Vector).
        *(void **)(parms + 0) = other;
        memcpy(parms + 4, hit, 12);
    }
    char sb[120], fb[160], ob[120];
    logf_("    [castgame] p%d %s: %s . %s (other=%s)", i, why,
          objName(self, sb, sizeof(sb)), objName(fn, fb, sizeof(fb)),
          other ? objName(other, ob, sizeof(ob)) : "None");
    g_ProcessEvent(self, NULL, fn, parms, NULL);
}

// The handler name a spell class maps to: LapiforsSpell -> HandleSpellLapifors.
static void *cgSpecificHandler(CgClass *ti, void *spellCls, char *nameOut, int cch)
{
    if (nameOut && cch > 0) nameOut[0] = 0;
    if (!ti || !spellCls || !ti->nSpec) return NULL;
    char cb[160]; objName(spellCls, cb, sizeof(cb));
    const char *sp = strchr(cb, ' ');
    char tok[64]; strncpy(tok, cgLastComp(sp ? sp + 1 : cb), 63); tok[63] = 0;
    size_t L = strlen(tok);
    if (L > 5 && !_stricmp(tok + L - 5, "Spell")) tok[L - 5] = 0;       // LapiforsSpell
    else if (L > 5 && !_strnicmp(tok, "spell", 5)) memmove(tok, tok + 5, L - 4); // spellLapifors
    if (!tok[0]) return NULL;
    for (int s = 0; s < ti->nSpec; s++) {
        if (!_stricmp(ti->specName[s] + 11, tok)) {
            if (nameOut) { strncpy(nameOut, ti->specName[s], cch - 1); nameOut[cch - 1] = 0; }
            return ti->fnSpec[s];
        }
    }
    return NULL;
}

static BOOL cgEngineDecl(const char *decl)
{
    return !decl || !decl[0] || !_strnicmp(decl, "Engine.", 7) || !_strnicmp(decl, "Core.", 5);
}

// Name-only fallback (class chain unavailable): "Function Pkg.<ClassToken>.<m>"
// for the actor's visible class token, else a known engine base.
static void *cgMethodByToken(void *actor, const char *method, const char *basePath,
                             const char *tokenFrag)
{
    char tok[96];
    void *fragHit = NULL;
    if (actor && objectClassToken(actor, tok, sizeof(tok)) && g_objArray) {
        int n = g_objArray->Num;
        char buf[300];
        for (int j = 0; j < n && n <= 400000; j++) {
            void *o = g_objArray->Data[j];
            if (!o) continue;
            objName(o, buf, sizeof(buf));
            if (strncmp(buf, "Function ", 9)) continue;
            const char *path = buf + 9;
            const char *last = strrchr(path, '.');
            if (!last || _stricmp(last + 1, method)) continue;
            const char *prev = last;
            while (prev > path && prev[-1] != '.') prev--;
            if ((int)strlen(tok) == (int)(last - prev) && !_strnicmp(prev, tok, last - prev))
                return o;
            if (tokenFrag && !fragHit && strncmp(path, "Engine.", 7)) {
                char ct[96]; int cl = (int)(last - prev);
                if (cl > 0 && cl < 95) { memcpy(ct, prev, cl); ct[cl] = 0;
                    if (ciHas(ct, tokenFrag)) fragHit = o; }
            }
        }
    }
    if (fragHit) return fragHit;
    return basePath ? findObjectByPath(basePath) : NULL;
}

// Deliver the spell to the target the way the game does when the projectile
// arrives (or via the wand's autohit): the spell's own ProcessTouch dispatches
// to the object's handler inside game code. Trigger-family targets get the
// Touch(spell) the engine would have sent. Direct handler calls are only the
// fallback when the spell exposes no ProcessTouch (or never spawned).
static void cgDeliverHit(CgPending *p, BOOL spellAlive)
{
    int i = p->player;
    void *spell = spellAlive ? p->spell : NULL;
    if (spell && !IsBadReadPtr((BYTE *)spell + F.Location, 12)) {
        // HitLocation = the projectile's position at impact, as the engine
        // would report it (hit effects spawn there, not inside the object).
        float *sl = (float *)((BYTE *)spell + F.Location);
        float dx = sl[0] - p->aim[0], dy = sl[1] - p->aim[1], dz = sl[2] - p->aim[2];
        if (dx * dx + dy * dy + dz * dz < 600.0f * 600.0f) memcpy(p->aim, sl, 12);
    }
    CgClass *ti = p->tinfo;
    CgClass *si = spell ? cgClassInfo(cgClassOf(spell)) : NULL;
    BOOL did = FALSE, gameDispatch = FALSE;
    char hn[48];
    BOOL trigLike = ti ? (ti->isTrigger || ti->nameTrigger) : ciHas(p->targetName, "Trigger");

    // 1. Trigger family: the engine's Touch(spell) is what a flying spell
    //    would have produced (IsRelevant checks the spell type inside).
    if (trigLike && spell) {
        void *fn = (ti && ti->fnTouch) ? ti->fnTouch
                 : cgMethodByToken(p->target, "Touch", "Engine.Trigger.Touch", "SpellTrigger");
        if (fn) {
            cgCall(i, p->target, fn, FALSE, spell, p->target, p->caster, p->spellCls,
                   p->aim, "trigger touch");
            did = TRUE; gameDispatch = TRUE;
        }
    }
    // 2. The spell's own ProcessTouch(target, hitLoc) - the wand's autohit.
    //    Only when the game overrides it (Engine.Projectile's is empty).
    if (spell) {
        void *fn = NULL;
        if (si && si->fnPTouch && !cgEngineDecl(si->pTouchDecl)) fn = si->fnPTouch;
        else if (!si) fn = cgMethodByToken(spell, "ProcessTouch", NULL, "Spell");
        if (fn) {
            cgCall(i, spell, fn, TRUE, spell, p->target, p->caster, p->spellCls,
                   p->aim, "spell ProcessTouch");
            did = TRUE; gameDispatch = TRUE;
        }
    }
    // 3. No game-side dispatch available: call the object's handler the way
    //    ProcessTouch would have (HandleSpell<Name>(spell, hitLoc), else the
    //    generic OnSpellHit/HandleSpell).
    if (!gameDispatch && ti) {
        void *fn = cgSpecificHandler(ti, p->spellCls, hn, sizeof(hn));
        if (fn) {
            cgCall(i, p->target, fn, FALSE, spell, p->target, p->caster, p->spellCls,
                   p->aim, "target handler");
            did = TRUE;
        } else if (ti->nGen) {
            cgCall(i, p->target, ti->fnGen[0], FALSE, spell, p->target, p->caster,
                   p->spellCls, p->aim, "target generic handler");
            did = TRUE;
        }
        // 4. Spell Touch (Engine.Projectile.Touch -> ProcessTouch inside the
        //    game) when nothing else exists.
        if (!did && si && si->fnTouch && spell) {
            cgCall(i, spell, si->fnTouch, TRUE, spell, p->target, p->caster,
                   p->spellCls, p->aim, "spell Touch");
            did = TRUE;
        }
    }
    if (!did)
        logf_("    [castgame] p%d %s: NO activation path (spell=%s ptouch=%s touch=%d "
              "handlers=%d/%d) - send this log", i, p->targetName,
              spell ? "alive" : "none", si && si->fnPTouch ? si->pTouchDecl : "none",
              si && si->fnTouch ? 1 : 0, ti ? ti->nSpec : 0, ti ? ti->nGen : 0);
}

static void cgArmHit(int i, void *caster, void *spawned, CgCand *c, void *spellCls)
{
    CgPending *p = &g_cgPend[i];
    if (p->active)
        logf_("    [castgame] p%d previous pending hit on %s dropped (new cast)", i, p->targetName);
    memset(p, 0, sizeof(*p));
    p->spellSlot = -1;
    if (!c || !cgCandAlive(c)) {
        logf_("    [castgame] p%d target gone before the fire completed - no pending hit", i);
        return;
    }
    p->active = TRUE; p->player = i; p->caster = caster; p->spellCls = spellCls;
    p->target = c->obj; p->targetSlot = c->slot; p->tinfo = c->info;
    strncpy(p->targetName, c->name, sizeof(p->targetName) - 1);
    cgAimPoint(c, p->aim);
    p->hitR = cgHitRadius(c) + 40.0f;
    if (p->hitR < 80.0f) p->hitR = 80.0f;
    p->fireAt = GetTickCount();
    float d = 0.0f, speed = 1000.0f;
    if (spawned && !IsBadReadPtr(spawned, F.Location + 12)) {
        p->spell = spawned;
        p->spellClsPtr = cgClassOf(spawned);
        objName(spawned, p->spellName, sizeof(p->spellName));
        if (g_objArray) {
            int n = g_objArray->Num;
            for (int j = 0; j < n && n <= 400000; j++)
                if (g_objArray->Data[j] == spawned) { p->spellSlot = j; break; }
        }
        float *sl = (float *)((BYTE *)spawned + F.Location);
        float dx = p->aim[0] - sl[0], dy = p->aim[1] - sl[1], dz = p->aim[2] - sl[2];
        d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (g_offProjSpeed > 0 && !IsBadReadPtr((BYTE *)spawned + g_offProjSpeed, 4)) {
            float s = *(float *)((BYTE *)spawned + g_offProjSpeed);
            if (s > 100.0f && s < 20000.0f) speed = s;
        }
    }
    p->d0 = d;
    DWORD flight = (DWORD)(d / speed * 1000.0f) + 700;
    if (flight < 900) flight = 900;
    if (flight > 3500) flight = 3500;
    p->deadline = p->fireAt + flight;
    if (p->spell && p->spellSlot < 0) {
        // Not in the object table under that pointer: cannot watch it fly,
        // so deliver right now (autohit) while the pointer is known-good.
        logf_("    [castgame] p%d spell actor not indexed - immediate hit on %s", i, p->targetName);
        cgDeliverHit(p, TRUE);
        p->active = FALSE;
        return;
    }
    if (!p->spell) {
        // Nothing to fly: the game could not spawn the projectile, so run the
        // handler directly (with spell=None, as the optional params allow).
        logf_("    [castgame] p%d no spell actor - direct handler on %s", i, p->targetName);
        cgDeliverHit(p, FALSE);
        p->active = FALSE;
        return;
    }
    logf_("    [castgame] p%d pending hit on %s: dist=%.0f speed=%.0f window=%lums hitR=%.0f",
          i, p->targetName, d, speed, (unsigned long)flight, p->hitR);
}

static BOOL cgSpellAlive(CgPending *p)
{
    if (!p->spell || !g_objArray || p->spellSlot < 0 || p->spellSlot >= g_objArray->Num) return FALSE;
    if (g_objArray->Data[p->spellSlot] != p->spell) return FALSE;
    if (IsBadReadPtr(p->spell, F.Location + 12)) return FALSE;
    if (p->spellClsPtr && cgClassOf(p->spell) != p->spellClsPtr) return FALSE;
    return !cgDeleted(p->spell);
}

// Per frame: watch each pending projectile; deliver when it reaches the
// target or the flight window closes with it still alive. A projectile that
// died on its own hit something naturally - the game already handled it.
static void cgTick(void)
{
    if (!g_castGameplay) return;
    DWORD now = GetTickCount();
    for (int i = 1; i < 8; i++) {
        CgPending *p = &g_cgPend[i];
        if (!p->active) continue;
        BOOL alive = cgSpellAlive(p);
        DWORD age = now - p->fireAt;
        if (!alive) {
            logf_("    [castgame] p%d spell %s ended on its own at +%lums (natural touch "
                  "or expiry) - target %s", i, p->spellName, (unsigned long)age, p->targetName);
            p->active = FALSE;
            continue;
        }
        float *sl = (float *)((BYTE *)p->spell + F.Location);
        float dx = p->aim[0] - sl[0], dy = p->aim[1] - sl[1], dz = p->aim[2] - sl[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        BOOL targetOk = (g_objArray && p->targetSlot >= 0 && p->targetSlot < g_objArray->Num &&
                         g_objArray->Data[p->targetSlot] == p->target &&
                         !IsBadReadPtr(p->target, F.Location + 12) &&
                         (!p->tinfo || cgClassOf(p->target) == p->tinfo->cls));
        if (!targetOk) {
            logf_("    [castgame] p%d target %s vanished - pending hit dropped", i, p->targetName);
            p->active = FALSE;
            continue;
        }
        if (age < 50) continue;          // natural collision gets first go
        if (d <= p->hitR || now >= p->deadline) {
            logf_("    [castgame] p%d spell reached %s: dist=%.0f (start %.0f) at +%lums%s",
                  i, p->targetName, d, p->d0, (unsigned long)age,
                  d <= p->hitR ? "" : " [flight window closed - forcing the hit]");
            if (g_castAutoHit) cgDeliverHit(p, TRUE);
            else logf_("    [castgame] p%d CastAutoHit=0 - leaving it to the game's collision", i);
            p->active = FALSE;
        }
    }
}

// The spell class the target asks for (the vulnerableToClass value), or the
// caster's own current spell when the object does not say.
static void *cgSpellClassFor(CgCand *c, void *fallback)
{
    if (c && c->spellCls && cgIsKnownClass(c->spellCls) && !cgGenericSpellClass(c->spellCls))
        return c->spellCls;
    return fallback;
}

// What the index knows about one live actor - used to describe the object
// PLAYER 1's own cursor selected, so the log proves whether our candidate
// rules would have found the same thing.
static void cgDescribeActor(void *o, const char *tag)
{
    char ob[160], cb[160];
    if (!o || IsBadReadPtr(o, 0x100)) { logf_("  [castgame] %s: <bad actor>", tag); return; }
    objName(o, ob, sizeof(ob));
    cgBuildIndex();
    CgClass *ci = g_cgChainOK ? cgClassInfo(cgClassOf(o)) : NULL;
    if (!ci) { logf_("  [castgame] %s: %s (no class info)", tag, ob); return; }
    void *v = NULL; int vb = -1;
    if (ci->vulnOff > 0 && !IsBadReadPtr((BYTE *)o + ci->vulnOff, 4)) {
        void *x = *(void **)((BYTE *)o + ci->vulnOff);
        if (cgIsKnownClass(x)) v = x;
    }
    if (ci->vulnByteOff > 0 && !IsBadReadPtr((BYTE *)o + ci->vulnByteOff, 1))
        vb = *((BYTE *)o + ci->vulnByteOff);
    BOOL inList = FALSE;
    for (int k = 0; k < g_cgNCand; k++) if (g_cgCand[k].obj == o) { inList = TRUE; break; }
    logf_("  [castgame] %s: %s class=%s %s=%s vulnByte=%d handlers=%d/%d touch=%d "
          "trigger=%d pawn=%d in-candidates=%d", tag, ob, ci->path,
          ci->vulnName[0] ? ci->vulnName : "vulnerableTo", v ? objName(v, cb, sizeof(cb)) : "None",
          vb, ci->nSpec, ci->nGen, ci->fnTouch ? 1 : 0, ci->isTrigger || ci->nameTrigger,
          ci->isPawn, inList);
    for (int s = 0; s < ci->nSpec; s++) logf_("      handler: %s", ci->specName[s]);
    for (int g = 0; g < ci->nGen; g++)  logf_("      handler: %s", ci->genName[g]);
}

// Watch PLAYER 1's real cast: when the game's own cursor writes his
// spellTarget / currentSpell, log what it chose and open a telemetry window
// so the [pelog] lines carry the genuine activation sequence. Cheap: two
// pointer reads per frame.
static void pelogAuto(void);
static void *findOrigCursor(void);
static void cgWatchP1(void)
{
    static void *sLastTarget = NULL, *sLastSpell = NULL, *sLastPawn = NULL;
    static DWORD sLastWindowAt = 0, sLastLogAt = 0;
    static DWORD sResolveAt = 0;
    void *p1 = g_pawn[0];
    // v57 crash fix: g_pawn[0] is cached across frames; the old level's Harry
    // is freed UObjects memory the moment travel finishes. A stale pawn passed
    // IsBadReadPtr and then crashed GetFullName inside actorInCurrentLevel on
    // real hardware (entering Hogwarts). Drop the cache before naming it.
    if (p1 && !cgLiveObject(p1)) { g_pawn[0] = NULL; p1 = NULL; sLastPawn = NULL; }
    if (!p1) {
        DWORD nowR = GetTickCount();
        if (nowR - sResolveAt < 3000) return;
        sResolveAt = nowR;
        p1 = findActorByClass("harry");
        if (p1) g_pawn[0] = p1;
    }
    if (!F.ok || !p1 || !actorInCurrentLevel(p1) || IsBadReadPtr(p1, 0x200)) return;
    if (p1 != sLastPawn) { sLastPawn = p1; sLastTarget = NULL; sLastSpell = NULL; }
    if (!g_cgReady && g_castGameplay && F.ok) cgBuildIndex();   // one pass per level
    if (g_offSpellTarget > 0 && !IsBadReadPtr((BYTE *)p1 + g_offSpellTarget, 4)) {
        void *t = *(void **)((BYTE *)p1 + g_offSpellTarget);
        DWORD now = GetTickCount();
        if (t != sLastTarget && now - sLastLogAt >= 300) {
            sLastLogAt = now;
            sLastTarget = t;
            if (t && !IsBadReadPtr(t, 0x100) && cgLiveObject(t)) {
                char tb[160];
                logf_("[castgame] PLAYER 1 spellTarget -> %s", objName(t, tb, sizeof(tb)));
                if (g_castGameplay) {
                    cgScanCandidates(p1, FALSE);
                    cgDescribeActor(t, "player-1 target");
                }
                if (g_cgP1Windows < 3 && now - sLastWindowAt > 12000 && !g_pelogOn) {
                    sLastWindowAt = now; g_cgP1Windows++;
                    pelogAuto();
                    logf_("[pelog] window %d/3 opened for player 1's cast (activation path capture)",
                          g_cgP1Windows);
                }
            } else {
                logf_("[castgame] PLAYER 1 spellTarget -> None");
            }
        }
    }
    if (g_offCurrentSpell > 0 && !IsBadReadPtr((BYTE *)p1 + g_offCurrentSpell, 4)) {
        void *s = *(void **)((BYTE *)p1 + g_offCurrentSpell);
        if (s != sLastSpell) {
            sLastSpell = s;
            char sb[160];
            logf_("[castgame] PLAYER 1 currentSpell -> %s",
                  s && !IsBadReadPtr(s, 0x30) && cgLiveObject(s)
                      ? objName(s, sb, sizeof(sb)) : "None");
        }
    }
    // The game's own cursor: SelectCursor.aCurrentTarget / aPossibleTarget
    // (KWGame names). Whatever it locks is by definition a castable object.
    static int sOffCur = -2, sOffPos = -2;
    static void *sLastCur = NULL, *sLastPos = NULL;
    if (sOffCur == -2) {
        sOffCur = propOffset("KWGame.SelectCursor.aCurrentTarget");
        if (sOffCur < 0) sOffCur = propOffset("hgame.SpellCursor.aCurrentTarget");
        sOffPos = propOffset("KWGame.SelectCursor.aPossibleTarget");
        if (sOffPos < 0) sOffPos = propOffset("hgame.SpellCursor.aPossibleTarget");
        logf_("[castgame] cursor target props: aCurrentTarget=+0x%X aPossibleTarget=+0x%X",
              sOffCur, sOffPos);
    }
    // Player 1's REAL cursor: his controller's Cursor property (KWGame.
    // KWHeroController.Cursor), else the level's SpellCursor actor.
    static int sOffCtrlCursor = -2;
    if (sOffCtrlCursor == -2) sOffCtrlCursor = propOffset("KWGame.KWHeroController.Cursor");
    void *cur = NULL;
    if (sOffCtrlCursor > 0 && F.PawnController > 0 && !IsBadReadPtr((BYTE *)p1 + F.PawnController, 4)) {
        void *ctrl = *(void **)((BYTE *)p1 + F.PawnController);
        if (ctrl && !IsBadReadPtr((BYTE *)ctrl + sOffCtrlCursor, 4))
            cur = *(void **)((BYTE *)ctrl + sOffCtrlCursor);
    }
    if (!cur) cur = findOrigCursor();
    // v57: cur can be a stale cursor that died with the previous level (the
    // controller Cursor field is read from a live pawn, but the cursor it
    // names may not be). Never name or dereference a dead cursor.
    if (cur && !cgLiveObject(cur)) cur = NULL;
    if (cur && !IsBadReadPtr(cur, 0x200) && cgClassOf(cur)) {
        static void *sSaidCur = NULL;
        if (cur != sSaidCur) {
            sSaidCur = cur;
            char cb[160];
            logf_("[castgame] PLAYER 1 cursor actor: %s", objName(cur, cb, sizeof(cb)));
        }
        if (sOffCur > 0 && !IsBadReadPtr((BYTE *)cur + sOffCur, 4)) {
            void *t = *(void **)((BYTE *)cur + sOffCur);
            if (t != sLastCur) {
                sLastCur = t;
                if (t && !IsBadReadPtr(t, 0x100) && cgLiveObject(t) && cgClassOf(t)) {
                    char tb[160];
                    logf_("[castgame] PLAYER 1 cursor aCurrentTarget -> %s", objName(t, tb, sizeof(tb)));
                    if (g_castGameplay) { cgScanCandidates(p1, FALSE); cgDescribeActor(t, "cursor target"); }
                    // A lock-on precedes the fire: open the capture now so
                    // the window holds the whole activation sequence.
                    DWORD nowC = GetTickCount();
                    if (g_cgP1Windows < 3 && nowC - sLastWindowAt > 12000 && !g_pelogOn) {
                        sLastWindowAt = nowC; g_cgP1Windows++;
                        pelogAuto();
                        logf_("[pelog] window %d/3 opened at player 1's cursor lock-on", g_cgP1Windows);
                    }
                } else if (!t) logf_("[castgame] PLAYER 1 cursor aCurrentTarget -> None");
            }
            // Sample every frame while the stock cursor stays locked. The two
            // companions can take a few ticks to enter their own held state.
            // v54 observed only while split-screen was OFF; a split-ON session
            // therefore never certified anything (no [coopcast] CERTIFIED),
            // so a P2/P3 >10-second hold could never enter the shared path.
            // The stock P1 cursor does lock targets during a live split session
            // (coopObserveP1 below applies the mode-appropriate evidence test).
            if (t && !IsBadReadPtr(t, 0x100) && cgLiveObject(t) && cgClassOf(t))
                coopObserveP1(p1, t);
            else
                coopObserveP1(p1, NULL);  // unlock/invalid target resets proof dwell
        }
        if (sOffCur <= 0 || IsBadReadPtr((BYTE *)cur + sOffCur, 4)) {
            coopObserveP1(p1, NULL);
        }
        // v56: P1's own uninterrupted >10-second cursor lock on the same
        // object also fires the cooperative path. The cursor lock is the
        // engine's "P1 is holding cast on this target" signal - the engine
        // already handles PressedFire/ReleasedFire/cast state on the
        // release; the mod borrows the other two heroes alongside. P1's
        // natural release still fires its single spell, the trio then
        // converges through coopMaintain until the holder releases.
        coopP1HoldTry(p1);
        // Drive the cooperative hold for P1: when the cursor is locked,
        // P1 is "holding cast"; when the cursor releases, the shared hold
        // restores (holder released). coopTick takes the same path as a
        // P2/P3 hold - maintain, restore, fire (no-op once coopStart ran).
        {
            BOOL p1Held = (g_p1CursorLockedTarget != NULL);
            coopTick(0, p1, p1Held);
        }
        if (sOffPos > 0 && !IsBadReadPtr((BYTE *)cur + sOffPos, 4)) {
            void *t = *(void **)((BYTE *)cur + sOffPos);
            if (t != sLastPos) {
                sLastPos = t;
                if (t && !IsBadReadPtr(t, 0x100) && cgLiveObject(t) && cgClassOf(t)) {
                    char tb[160];
                    logf_("[castgame] PLAYER 1 cursor aPossibleTarget -> %s", objName(t, tb, sizeof(tb)));
                    if (g_castGameplay) { cgScanCandidates(p1, FALSE); cgDescribeActor(t, "cursor possible target"); }
                }
            }
        }
    } else {
        coopObserveP1(p1, NULL);   // no live P1 cursor: reset any pending dwell
    }
}

static BOOL coopClassMatchesProof(void *cls)
{
    if (!cls || !cgIsKnownClass(cls)) return FALSE;
    for (int p = 0; p < g_nCoopProof; p++) {
        void *walk = cls;
        for (int depth = 0; walk && depth < 32; depth++) {
            if (walk == g_coopProof[p].cls) return TRUE;
            walk = cgSuper(walk);
        }
    }
    return FALSE;
}

// v58: the CompanionSpellTrigger class family - the level puzzle object the
// stock game itself recruits Hermione and Ron for (CompanionSpellTrigger0/2/10
// in the v56 hardware pelogs). The name token is checked on the class chain so
// map-specific subclasses qualify. Results are memoized: the trio gate sits in
// the per-tick hold path and GetFullName is not cheap there. Class UObjects
// live in the package and survive map travel; each cached hit is re-validated
// against the live class table to stay level-travel-safe.
static BOOL coopClassHasCompanionToken(void *cls)
{
    if (!cls || !cgIsKnownClass(cls)) return FALSE;
    enum { FAM_CACHE = 16 };
    static void *sFamCls[FAM_CACHE]; static BOOL sFamYes[FAM_CACHE]; static int sNFam = 0;
    for (int k = 0; k < sNFam; k++)
        if (sFamCls[k] == cls && cgIsKnownClass(cls)) return sFamYes[k];
    BOOL yes = FALSE;
    void *walk = cls;
    for (int depth = 0; walk && depth < 24 && cgIsKnownClass(walk); depth++) {
        char full[260];
        objName(walk, full, sizeof(full));
        if (ciHas(full, "CompanionSpellTrigger")) { yes = TRUE; break; }
        walk = cgSuper(walk);
    }
    if (sNFam < FAM_CACHE) { sFamCls[sNFam] = cls; sFamYes[sNFam] = yes; sNFam++; }
    return yes;
}

// v58: the trio hold is re-gated to the game's genuine cooperative class
// family - CompanionSpellTrigger (the observed trio-cast puzzle) plus any
// class certified this session by the strict all-three companion observation.
// v57 armed the shared hold for ANY living cast candidate (a pumpkin, a
// spawner, any vulnerableToClass object): every casual 10-second player hold
// then borrowed the AI heroes out of their follow/cast behaviour - Ron
// visibly run-running back and forth to "cast together" mid-level was this
// gate missing, not his AI.
static BOOL coopClassIsCooperative(void *cls)
{
    if (!g_cgChainOK || !cls || !cgIsKnownClass(cls)) return FALSE;
    return coopClassMatchesProof(cls) || coopClassHasCompanionToken(cls);
}

// A class is certified from behaviour, not its name: P1's unmodified cursor
// is holding this exact object and BOTH of the real companions have independently
// selected it too. This filters ordinary one-person SpellTriggers without
// needing a brittle guessed class/property name from an unavailable package.
static void coopObserveP1(void *p1, void *cursorTarget)
{
    if (!p1) {
        g_coopProofPendingTarget = g_coopProofPendingClass = NULL;
        g_coopProofPendingSince = 0;
        g_p1CursorLockedTarget = NULL; g_p1CursorLockedAt = 0; g_p1CursorFired = 0;
        g_p1CursorLastSeen = 0;
        return;
    }
    DWORD now = GetTickCount();
    BOOL validTarget = cursorTarget && g_offSpellTarget > 0 &&
                       actorInCurrentLevel(cursorTarget) &&
                       !cgDeleted(cursorTarget) &&
                       cgIsKnownClass(cgClassOf(cursorTarget));

    // v56: P1 cursor flicker tolerance. The stock game cursor can briefly
    // report None or a different object between controller updates (the
    // v55 hardware log showed CompanionSpellTrigger10 -> None -> back in
    // adjacent frames). Resetting every dwell timer on a single None frame
    // means a deliberate 10+ second P1 hold never satisfies either the cert
    // (1.2s split-ON) or the 10s P1 hold path. The grace window is small
    // (250 ms) so a real cursor that has moved away is still treated as
    // moved away; a held cursor with frame-level flicker is not.
    if (validTarget) {
        g_p1CursorLastSeen = now;
        if (g_p1CursorLockedTarget != cursorTarget) {
            // a *new* object (or the first one) - reset everything unless
            // the previous target just disappeared briefly (covered by the
            // grace check below at the None branch)
            g_p1CursorLockedTarget = cursorTarget;
            g_p1CursorLockedAt = now;
            g_p1CursorFired = 0;
            g_coopProofPendingTarget = g_coopProofPendingClass = NULL;
            g_coopProofPendingSince = 0;
        }
    } else if (g_p1CursorLockedTarget) {
        if ((DWORD)(now - g_p1CursorLastSeen) > kP1CursorGrace) {
            // gap longer than grace: the cursor really has moved away
            g_p1CursorLockedTarget = NULL;
            g_p1CursorLockedAt = 0;
            g_p1CursorFired = 0;
            g_coopProofPendingTarget = g_coopProofPendingClass = NULL;
            g_coopProofPendingSince = 0;
        }
        // otherwise: brief None - keep the lock state intact
    }

    if (!validTarget) {
        // a brief None while we have a pending cert is not a hard reset;
        // a real absence (gap > grace) already cleared the state above.
        if (g_p1CursorLockedTarget == NULL) {
            g_coopProofPendingTarget = g_coopProofPendingClass = NULL;
            g_coopProofPendingSince = 0;
        }
        // The proof's pendingSince follows the lock's first-seen-at so a
        // frame-level None does not restart the cert dwell.
        if (g_p1CursorLockedTarget == g_coopProofPendingTarget &&
            g_p1CursorLockedAt && g_coopProofPendingSince == 0) {
            g_coopProofPendingSince = g_p1CursorLockedAt;
        }
        return;
    }
    void *cls = cgClassOf(cursorTarget);
    if (!cls || !cgIsKnownClass(cls)) return;

    // v58: a split-ON cursor lock alone must not certify just ANY castable
    // class - a casual 1.2-second P1 lock on a pumpkin used to enter the
    // proof table and then admitted that class to the trio hold forever,
    // which is one of the ways Ron kept getting hijacked by ordinary holds.
    // Split-ON certification is limited to the CompanionSpellTrigger family;
    // the strict all-three split-OFF observation remains the only way to
    // certify a non-family class.
    if (g_splitOn && !coopClassHasCompanionToken(cls)) return;

    // v54 certified a class only while split-screen was OFF: P1's cursor had
    // to be on the object AND both AI companions' pawn spellTarget had to
    // point at it too. During a live split session the companions are under
    // mod/AI control and their pawn spellTarget never demonstrates a shared
    // hold, so that gate could never fire - a split-ON session never emitted
    // [coopcast] CERTIFIED and a >10-second P2/P3 hold stayed a plain cast.
    // The mode-appropriate evidence:
    //   * split OFF  - the strict all-three demonstration (unchanged), and
    //   * split ON   - P1's genuine stock cursor lock on the object is the
    //     live verification (this function is fed only by that cursor). The
    //     lock alone admits the class; the three-character CAST is still
    //     entered by any real P2/P3 holder's uninterrupted >10-second hold,
    //     so no player-1-only ceremony is required mid-session.
    if (!g_splitOn) {
        void *hermione = getPawn(1), *ron = getPawn(2);
        if (!hermione || !ron || IsBadReadPtr(hermione, 0x100) || IsBadReadPtr(ron, 0x100) ||
            !actorInCurrentLevel(hermione) || !actorInCurrentLevel(ron) ||
            cgDeleted(hermione) || cgDeleted(ron) ||
            IsBadReadPtr((BYTE *)hermione + g_offSpellTarget, 4) ||
            IsBadReadPtr((BYTE *)ron + g_offSpellTarget, 4)) {
            g_coopProofPendingTarget = g_coopProofPendingClass = NULL; g_coopProofPendingSince = 0;
            return;
        }
        if (*(void **)((BYTE *)hermione + g_offSpellTarget) != cursorTarget ||
            *(void **)((BYTE *)ron + g_offSpellTarget) != cursorTarget) {
            g_coopProofPendingTarget = g_coopProofPendingClass = NULL; g_coopProofPendingSince = 0;
            return;
        }
    }
    if (g_coopProofPendingTarget != cursorTarget || g_coopProofPendingClass != cls) {
        g_coopProofPendingTarget = cursorTarget; g_coopProofPendingClass = cls;
        // Anchor cert dwell to the cursor lock's first-seen-at so frame-level
        // None/different-target flickers inside the grace window do not reset
        // the dwell. The lock state was cleared/reset by the cursor block
        // above; we only get here when cursorTarget is the same as
        // g_p1CursorLockedTarget.
        g_coopProofPendingSince = g_p1CursorLockedAt ? g_p1CursorLockedAt : now;
        return;
    }
    // Let one full controller update settle before recording proof; a single
    // stale target pointer must not certify a class for the rest of the map.
    // A split-ON lock is weaker evidence than the all-three split-OFF test
    // (no companions confirming the shared hold), so it must be a deliberate,
    // sustained lock rather than a cursor that merely brushed the object.
    DWORD minDwell = g_splitOn ? 1200u : 350u;
    if ((DWORD)(now - g_coopProofPendingSince) < minDwell) return;
    for (int p = 0; p < g_nCoopProof; p++)
        if (g_coopProof[p].cls == cls) return;
    if (g_nCoopProof >= (int)(sizeof(g_coopProof) / sizeof(g_coopProof[0]))) {
        logf_("[coopcast] proof table full; ignoring additional P1 cooperative class");
        return;
    }
    CoopClassProof *proof = &g_coopProof[g_nCoopProof++];
    proof->cls = cls; proof->observedAt = now;
    char full[180];
    objName(cls, full, sizeof(full));
    const char *sp = strchr(full, ' ');
    strncpy(proof->path, sp ? sp + 1 : full, sizeof(proof->path) - 1);
    proof->path[sizeof(proof->path) - 1] = 0;
    char targetName[180];
    if (g_splitOn)
        logf_("[coopcast] CERTIFIED cooperative class %s from Player 1's live cursor lock on %s (split-screen ON); a >10-second P2/P3 hold on this class may now enter the shared hold in this level",
              proof->path, objName(cursorTarget, targetName, sizeof(targetName)));
    else
        logf_("[coopcast] CERTIFIED P1 cooperative class %s after Harry, Hermione and Ron held %s; P2/P3 fallback may now use this class in this level",
              proof->path, objName(cursorTarget, targetName, sizeof(targetName)));
}


// Pick something worth casting at. A companion's natural targets in this game
// are the level's spell triggers (HP3_InsideHub ships eight of them), so the
// aim search walks the object table for those, keeps the ones inside a forward
// cone and within reach, and confirms line of sight with a FastTrace.
// A spell target is either one of the level's cast triggers or another
// character. Self is never a candidate.
static BOOL isAimCandidate(const char *fullName, void *o, void *self)
{
    if (o == self) return FALSE;
    if (strstr(fullName, "SpellTrigger ")) return TRUE;
    for (int c = 0; c < 8 && kCharClass[c]; c++) {
        size_t L = strlen(kCharClass[c]);
        if (!strncmp(fullName, kCharClass[c], L) && fullName[L] == ' ') return TRUE;
    }
    return FALSE;
}

// v18: exclude the OTHER players' pawns from the auto-aim cone - in split
// screen they are always within reach of each other and the cone happily
// locked onto Ron/Harry instead of what the caster is looking at.
static void *findAimTarget(void *pawn, float *outDist, BOOL verbose)
{
    if (!g_objArray || F.Location <= 0) return NULL;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000) return NULL;

    float *pl = (float *)((BYTE *)pawn + F.Location);
    int   *pr = (int *)  ((BYTE *)pawn + F.Rotation);
    double yaw = pr[1] * (6.283185307179586 / 65536.0);
    float fx = (float)cos(yaw), fy = (float)sin(yaw);
    float chest[3] = { pl[0], pl[1], pl[2] + 40.0f };

    const float REACH = 3000.0f, MINDOT = 0.5f;   // ~60 degree cone
    void *best = NULL; float bestScore = -1.0f, bestDist = 0.0f;
    char buf[300];

    for (int i = 0; i < n; i++) {
        void *o = g_objArray->Data[i];
        if (!o) continue;
        BOOL isPlayer = FALSE;
        for (int p = 0; p < 8; p++) if (g_pawn[p] && g_pawn[p] == o) { isPlayer = TRUE; break; }
        if (isPlayer) continue;
        objName(o, buf, sizeof(buf));
        if (!isAimCandidate(buf, o, pawn)) continue;
        if (IsBadReadPtr(o, F.Location + 12)) continue;
        float *tl = (float *)((BYTE *)o + F.Location);
        float dx = tl[0] - pl[0], dy = tl[1] - pl[1], dz = tl[2] - pl[2];
        float d = (float)sqrt(dx * dx + dy * dy + dz * dz);
        if (d < 1.0f || d > REACH) continue;
        float dot = (dx * fx + dy * fy) / d;               // forward cone test
        if (dot < MINDOT) continue;
        float tgt[3] = { tl[0], tl[1], tl[2] };
        if (!fastTraceClear(pawn, chest, tgt)) continue;   // needs line of sight
        float score = dot - d / (REACH * 4.0f);            // straight ahead & near
        if (score > bestScore) { bestScore = score; best = o; bestDist = d; }
    }
    // Nothing in the cone: report the nearest candidate anyway, so a run that
    // finds no target can be told apart from a level that simply has none.
    if (!best && verbose) {
        void *nearestTrig = NULL; float nd = 1e9f; int seen = 0;
        for (int i = 0; i < n; i++) {
            void *o = g_objArray->Data[i]; if (!o) continue;
            objName(o, buf, sizeof(buf));
            if (!strstr(buf, "SpellTrigger ")) continue;
            if (IsBadReadPtr(o, F.Location + 12)) continue;
            seen++;
            float *tl = (float *)((BYTE *)o + F.Location);
            float dx = tl[0]-pl[0], dy = tl[1]-pl[1], dz = tl[2]-pl[2];
            float d = (float)sqrt(dx*dx + dy*dy + dz*dz);
            if (d < nd) { nd = d; nearestTrig = o; }
        }
        if (nearestTrig) logf_("    aim: %d spell triggers in level, nearest %s at %.0f units",
                        seen, objName(nearestTrig, buf, sizeof(buf)), nd);
        else      logf_("    aim: no spell triggers in this level at all");
    }
    if (outDist) *outDist = bestDist;
    return best;
}

// A companion pawn has no spell selected, which is exactly why PressedFire
// produced no effect. Borrow the lead character's currentSpell class (or any
// spell class we found) and select it on this pawn first.
static void *spellClassFor(void *pawn)
{
    if (g_offCurrentSpell <= 0 || !pawn) return NULL;
    void *cls = *(void **)((BYTE *)pawn + g_offCurrentSpell);
    if (cls) return cls;
    if (g_pawn[0] && !IsBadReadPtr(g_pawn[0], g_offCurrentSpell + 4)) {
        cls = *(void **)((BYTE *)g_pawn[0] + g_offCurrentSpell);
        if (cls) return cls;
    }
    return g_defaultSpell;
}

// Player 2..N spellcast, through the pawn's own script so animation, sound and
// the spell actor all spawn the way the game intends.
// A cast that is started but never ended leaves its effect actor attached to
// the caster -- the "bright red light stuck under player 2". The game normally
// ends the cast from its own state machine, which companions do not run while
// we are driving them, so we close it ourselves a beat later.
// ===========================================================================
// v48: ONE RECOVERY RUN PER CAST.
//
// Every post-cast fix used to live in function-local statics inside
// finishPendingCasts() that were only cleared when a cast's 4s watch window
// expired. A cast that began before the previous one's window ended therefore
// inherited "chain already fired / already verified" and got NO exit at all:
// the pawn sat in the cast pose until some later retry happened to fire. That
// is the "stuck in the casting pose from time to time" report, and it is easy
// to hit - just cast again within four seconds (v47 hw log, second cast of
// the first window: no [castclean] line at all, pose parked for 3 seconds
// until a stale retry rescued it).
//
// The rules now:
//   * a run is created the moment the spell leaves the caster;
//   * it is abandoned the instant a newer cast owns the pawn (the cast-begin
//     stamp moves) - the newer cast gets its own run and its own exit;
//   * every step is bound to that run, so nothing can ever act on another
//     cast's timebase.
// One struct, one place, reset on every cast.
// ===========================================================================
typedef struct {
    int   id;         // serial, so log lines group per cast
    void *pawn;       // never apply a run to a replacement actor in this slot
    DWORD fireAt;     // tick the spell left the caster (0 = no run)
    DWORD beginAt;    // cast-begin stamp this run belongs to
    DWORD chainAt;    // tick our exit chain fired (0 = not yet)
    DWORD exitAt;     // tick the state actually left (EndState / Tick stopped)
    DWORD lastTryAt;  // tick of the last exit we issued
    int   retries;    // extra exits issued after the chain (max 2)
    float fireZ;      // telemetry only; NEVER a floor reference
    BYTE  phys0;      // physics at fire (telemetry only)
    CastGround::Recovery ground; // one native floor handoff per exit attempt
    BOOL  rePicked;   // anim re-pick done (this settles the pose + facing)
} CastRun;
static CastRun g_run[8];
static int     g_runSeq = 0;

// Legacy v38 animation-only fallback, used only when NoDip=0. v49's default
// repairs floor contact and does not swallow real falling/landing animations.
static DWORD g_suppressLandUntil[8] = {0}; // GetTickCount()+1500 at arm
static DWORD g_hopkillT0[8]         = {0}; // log base: the chain-fire moment
// v39 state telemetry, harvested while the event detour is installed (the state
// pointer lives in a heap frame - a pawn scan is useless on hw - but the
// state's own script events are all visible there):
static DWORD g_animEndAt[8]   = {0}; // first post-release StateCasting.AnimEnd
static DWORD g_lastMoveAt[8]  = {0}; // v46: last tick with movement input
static int   g_castExitAdj    = 90;  // v40: AnimEnd->exit gap (ini CastExitAdj)
static int   g_noDip          = 1;   // v49: native floor handoff (NoDip)
static int   g_meshOff        = -2;  // v41: Engine.Actor.Mesh property offset
static int   g_viewYaw[8]     = {0}; // (moved up from driveePawn for v41)
static BOOL  g_jumpStanding[8] = {0}; // v12: a standing jump is in flight
static int   g_jumpPh[8]       = {0}; // v12: 0 none, 1 walk-prep, 2 airborne
static DWORD g_lastAirAt[8]   = {0}; // v42: last tick the pawn was airborne
                                     // (phys==2); forced exits need a 1.2s
                                     // clearance after the last air frame
static DWORD g_holdWalkUntil[8] = {0}; // v43: preemptive Walking hold window
static BOOL  g_holdArmed[8]     = {0}; // v43: a 5/6 cluster is being held
static BOOL  g_holdResync[8]    = {0}; // v43: anim re-sync done for cluster

// v41: mesh component yaw minus actor yaw, wrapped to -32768..32767.
// -99999 = unreadable (mesh prop unresolved / bad pointer).
static int meshYawDelta(void *pawn, int actorYaw)
{
    if (g_meshOff <= 0 || F.Rotation <= 0 || !pawn ||
        IsBadReadPtr(pawn, g_meshOff + 4))
        return -99999;
    void *mesh = *(void **)((BYTE *)pawn + g_meshOff);
    if (!mesh || IsBadReadPtr(mesh, F.Rotation + 12))
        return -99999;
    int my = *(int *)((BYTE *)mesh + F.Rotation + 4);
    int d = (my - actorYaw) & 65535;
    if (d > 32767) d -= 65536;
    return d;
}
static DWORD g_castTickAt[8]  = {0}; // last StateCasting.Tick seen (alive = stuck)
static DWORD g_beginCastAt[8] = {0}; // last cast begin (StateCasting.BeginState
                                     // OR the 100%-reliable HPCharacter.
                                     // StartCasting - Wine only logs
                                     // BeginState ~50% of the time)
static void armHopKill(int i)
{
    if (i < 0 || i >= 8) return;
    DWORD now = GetTickCount();
    g_suppressLandUntil[i] = g_noDip ? 0 : now + 1500;
    g_hopkillT0[i]         = now;
}
static void *g_castActor[8]   = {NULL};
static float g_castSpawnLoc[8][3];
static BOOL  g_castLoggedFly[8] = {0};
extern volatile LONG g_p2Moving[8];   // set per frame by driveePawn (below)
static BOOL aimGlowAlive(int i);      // fwd: glow actor alive == aiming now
static DWORD g_aimFXAt[8] = {0};      // v29: glow spawn tick (race fix:
                                      // only THIS cast's glow may block the
                                      // cleanup chain - a newer one is the
                                      // next aim and must not)
static float g_aimLast[8][3];         // v26: real aim point (pane restore)

// ---------------------------------------------------------------------------
// v49: repair FLOOR CONTACT, not height. The v48 hardware log shows that every
// reset was Z 874.75 -> 874.75, followed by another Falling/BaseChange on the
// next walking tick. Idle then froze PHYS_None before physics could settle.
// The eventual dip happened at movement start, even seconds after "done".
//
// Capture at EXIT (not fire: the player can walk during a cast). Re-enter
// walking through the native and probe the CURRENT collision support. Keep
// Walking alive for a few frames before the normal idle freeze is allowed.
// Never copy back Location/Base/Floor, and never retry a recurrent fall.
// ---------------------------------------------------------------------------
static CastGround::Sample castGroundSample(int i, void *pawn)
{
    CastGround::Sample s = {};
    s.jumping = keyDown(g_pk[i].jump) || keyDown(g_pk[i].jump2) ||
                g_padJump[i] || g_jumpStanding[i] || g_jumpPh[i] != 0;
    if (!pawn || F.Location <= 0 || F.Velocity <= 0 || F.Physics <= 0 ||
        IsBadReadPtr((BYTE *)pawn + F.Location, 12) ||
        IsBadReadPtr((BYTE *)pawn + F.Velocity, 12) ||
        IsBadReadPtr((BYTE *)pawn + F.Physics, 1)) return s;
    if (g_offDeleteMe > 0 && g_maskDeleteMe &&
        !IsBadReadPtr((BYTE *)pawn + (g_offDeleteMe & ~3), 4) &&
        (*(DWORD *)((BYTE *)pawn + (g_offDeleteMe & ~3)) & g_maskDeleteMe)) return s;
    memcpy(s.location, (BYTE *)pawn + F.Location, 12);
    // Basic motion validity is independent of the optional floor fields:
    // missing floor reflection must NOT disable the proven pose re-pick.
    if (F.Floor > 0 && !IsBadReadPtr((BYTE *)pawn + F.Floor, 12))
        memcpy(s.floor, (BYTE *)pawn + F.Floor, 12);
    s.vz = ((float *)((BYTE *)pawn + F.Velocity))[2];
    s.physics = *((BYTE *)pawn + F.Physics);
    if (F.Base > 0 && !IsBadReadPtr((BYTE *)pawn + F.Base, sizeof(void *)))
        s.base = *(void **)((BYTE *)pawn + F.Base);
    if (s.base && (IsBadReadPtr(s.base, 0x30) ||
        (g_offDeleteMe > 0 && g_maskDeleteMe &&
         !IsBadReadPtr((BYTE *)s.base + (g_offDeleteMe & ~3), 4) &&
         (*(DWORD *)((BYTE *)s.base + (g_offDeleteMe & ~3)) & g_maskDeleteMe))))
        s.base = NULL;
    s.valid = true;
    return s;
}

static void castGroundArm(int i, void *pawn, CastRun *r, DWORD now)
{
    memset(&r->ground, 0, sizeof(r->ground));
    if (!g_noDip) return;
    CastGround::Sample s = castGroundSample(i, pawn);
    CastGround::arm(r->ground, s, now);
    if (!g_opsOK || g_opByteConst < 0 || !g_execSetPhysics ||
        F.Base <= 0 || F.Floor <= 0)
        CastGround::release(r->ground, CastGround::Unavailable);
    logf_("    [castground] p%d run#%d %s (%s, exitZ=%.2f phys=%u "
          "base=%p floorZ=%.3f)", i, r->id,
          CastGround::phaseName(r->ground.phase),
          CastGround::reasonName(r->ground.reason), s.location[2],
          (unsigned)s.physics, s.base, s.floor[2]);
}

// Called by driveePawn AFTER fresh input is sampled, BEFORE its airborne
// bookkeeping and idle freeze. TRUE means leave walking physics on this frame.
static BOOL castGroundStep(int i, void *pawn, DWORD now)
{
    CastRun *r = &g_run[i];
    if (!g_noDip || !r->fireAt || r->pawn != pawn ||
        g_beginCastAt[i] != r->beginAt || !CastGround::active(r->ground))
        return FALSE;
    int runId = r->id;
    BOOL wasVerifying = r->ground.phase == CastGround::Verifying;
    CastGround::Sample before = castGroundSample(i, pawn);
    CastGround::Action action = CastGround::step(r->ground, before, now);
    if (action == CastGround::Reacquire) {
        if (!nativeSetPhysics(pawn, 1)) {
            CastGround::release(r->ground, CastGround::Unavailable);
        } else {
            if (r->id != runId || r->pawn != pawn ||
                r->beginAt != g_beginCastAt[i]) return FALSE;
            // SetPhysics can be a no-op if the script already chose Walking.
            // FindBase explicitly re-probes in that case as well. Its native
            // collision query supplies the base/normal; no saved actor pointer
            // or guessed flat-floor vector is ever installed by the mod.
            CastGround::Sample after = castGroundSample(i, pawn);
            if (g_FindBase && after.valid && after.physics == 1) {
                g_FindBase(pawn, NULL);
                if (r->id != runId || r->pawn != pawn ||
                    r->beginAt != g_beginCastAt[i]) return FALSE;
                after = castGroundSample(i, pawn);
            }
            BOOL accepted = CastGround::repaired(r->ground, after);
            // Zero only the incidental downward velocity, and only AFTER the
            // engine found walkable support. Positive velocity/jumps veto it.
            if (accepted && !IsBadWritePtr((BYTE *)pawn + F.Velocity, 12))
                ((float *)((BYTE *)pawn + F.Velocity))[2] = 0.0f;
            logf_("    [castground] p%d run#%d reacquire phys=%u->%u "
                  "base=%p->%p floor=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) "
                  "Z=%.2f->%.2f (%s)", i, r->id,
                  (unsigned)before.physics, (unsigned)after.physics,
                  before.base, after.base,
                  before.floor[0], before.floor[1], before.floor[2],
                  after.floor[0], after.floor[1], after.floor[2],
                  before.location[2], after.location[2],
                  accepted ? "verify walking" : CastGround::reasonName(r->ground.reason));
            if (accepted) return TRUE;
            // The probe found no floor. Undo OUR walking transition rather
            // than letting idle freeze an unsupported pawn over a ledge.
            if (after.valid && after.physics == 1 && !after.jumping &&
                after.vz <= 0.0f && !CastGround::supported(after))
                nativeSetPhysics(pawn, 2);
        }
    } else if (action == CastGround::KeepWalking) {
        return TRUE;
    } else if (action == CastGround::Settled) {
        logf_("    [castground] p%d run#%d verified (Walking %u frames / %lums, "
              "Z=%.2f base=%p floorZ=%.3f)", i, r->id,
              r->ground.walkingFrames, (unsigned long)(now - r->ground.repairAt),
              before.location[2], before.base, before.floor[2]);
        return FALSE;
    } else if (action != CastGround::GiveUp) {
        return FALSE;
    } else if (wasVerifying && before.valid && before.physics == 1 &&
               !before.jumping && before.vz <= 0.0f &&
               !CastGround::supported(before)) {
        // A later frame lost its base. Do not let idle turn that unsupported
        // Walking into None either; let the next physics tick fall normally.
        nativeSetPhysics(pawn, 2);
    }
    logf_("    [castground] p%d run#%d released (%s, phys=%u vz=%.1f) - "
          "engine owns gravity", i, r->id, CastGround::reasonName(r->ground.reason),
          (unsigned)before.physics, before.vz);
    return FALSE;
}

// ---------------------------------------------------------------------------
// v48: the whole post-cast recovery, in one place, one run per cast.
//
//   FIRE    the spell leaves; the pawn is parked in StateCasting (frozen).
//   EXIT    at AnimEnd+CastExitAdj (fallback CleanupDelay) run the proven
//           exit chain: GotoDefaultGroundMovementState,
//           blendOutCast, StopCasting, SwitchToNormalStanceAnims, AnimEnd.
//   VERIFY  StateCasting.EndState seen, or its Tick stopped = the state is
//           gone. While it is still alive, re-issue the exit (max 2, 700ms
//           apart): a skipped exit is what parks the cast pose on screen.
//   SETTLE  hand floor contact to native walking physics (v49), then - as
//           soon as the pawn stands still - re-pick the animation ONCE. That
//           re-pick settles the pose and the facing; the engine used to do it
//           inside the landing animation, which is exactly why a hop "fixed"
//           both. It stays armed for the rest of the run, so a player who
//           walks away and stops later still gets it.
//   DONE    4s after the fire, or the moment a newer cast takes the pawn.
// ---------------------------------------------------------------------------
#define CAST_WATCH_MS  4000     // how long a run stays alive after the fire
#define CAST_EXIT_MAX  1600     // hard ceiling on the scheduled exit delay

// A new cast begins its own run. Called from fireCast(), the one place where
// we know a spell actually left the caster (SpawnSpell returned a live actor).
static void castRunStart(int i, void *pawn)
{
    CastRun *r = &g_run[i];
    if (r->fireAt)      // a cast fired before the previous one finished
        logf_("    [cast] p%d run#%d dropped (new cast fired over it, "
              "age=%lums, %s)", i, r->id,
              (unsigned long)(GetTickCount() - r->fireAt),
              r->exitAt ? "it had exited" : "IT HAD NOT EXITED YET");
    memset(r, 0, sizeof(*r));
    r->id      = ++g_runSeq;
    r->pawn    = pawn;
    r->fireAt  = GetTickCount();
    r->beginAt = g_beginCastAt[i];
    if (pawn && F.Location > 0 && !IsBadReadPtr(pawn, F.Location + 12)) {
        float *pl = (float *)((BYTE *)pawn + F.Location);
        r->fireZ = pl[2];
    }
    r->phys0 = (F.Physics > 0 && !IsBadReadPtr((BYTE *)pawn + F.Physics, 1))
             ? *((BYTE *)pawn + F.Physics) : 0;
    logf_("    [cast] p%d run#%d fired (Z=%.2f phys=%d begin=%lu)",
          i, r->id, r->fireZ, (unsigned)r->phys0,
          (unsigned long)r->beginAt);
}

static void castDropRun(int i, const char *why)
{
    CastRun *r = &g_run[i];
    if (!r->fireAt) return;
    logf_("    [cast] p%d run#%d done (%s, age=%lums, chain=%d exit=%d "
          "retry=%d ground=%s repick=%d)",
          i, r->id, why, (unsigned long)(GetTickCount() - r->fireAt),
          r->chainAt ? 1 : 0, r->exitAt ? 1 : 0,
          r->retries, CastGround::phaseName(r->ground.phase), r->rePicked ? 1 : 0);
    memset(r, 0, sizeof(*r));
    g_castActor[i]   = NULL;
}

// The exit chain. The call set is the hardware-proven v23/v30 full chain
// (every reduction ever tried hung the pose). v49 changes only the physics
// handoff around it, not the calls or the timing that fixed the pose.
static void castExitChain(int i, void *pawn, CastRun *r, const char *tag)
{
    BYTE p[8]; memset(p, 0, sizeof(p));
    if (!g_cleanupLight && g_fnGotoGround)
        castGroundArm(i, pawn, r, GetTickCount()); // snapshot the EXIT position
    armHopKill(i);                      // legacy animation-only fallback
    if (!g_cleanupLight && g_fnGotoGround)
        callFn(pawn, g_fnGotoGround, "GotoDefaultGroundMovementState()");
    if (!g_cleanupLight && g_fnBlendOut && g_cleanupChain != 3)
        callFnP(pawn, g_fnBlendOut, p, 4, "blendOutCast()");
    if (g_fnStopCast && g_cleanupChain != 3)
        callFnP(pawn, g_fnStopCast, p, 4, "StopCasting()");
    if (g_fnSwitchNormal && g_cleanupChain == 0)
        callFnP(pawn, g_fnSwitchNormal, p, 4, "SwitchToNormalStanceAnims()");
    if (g_fnAnimEnd && (g_cleanupChain == 0 || g_cleanupChain == 3))
        callFn(pawn, g_fnAnimEnd, "AnimEnd");
    logf_("    [cast] p%d run#%d exit chain (%s, fire+%lums)", i, r->id, tag,
          (unsigned long)(GetTickCount() - r->fireAt));
}

// When does this cast's exit run? Right after the cast animation's own end
// (AnimEnd + CastExitAdj) when we saw one - that timing is what removed the
// pose snap - otherwise the configured delay. Bounded both ways so a missing
// AnimEnd (Wine never emits one, v39) or a very late one can never push the
// exit out of reach.
static DWORD castExitDelay(int i, CastRun *r)
{
    DWORD d = (DWORD)g_cleanupDelay;
    if (g_animEndAt[i] > r->fireAt && g_animEndAt[i] - r->fireAt < 6000) {
        DWORD endAge = (g_animEndAt[i] - r->fireAt) + (DWORD)g_castExitAdj;
        if (endAge >= 400 && endAge < d) d = endAge;
    }
    if (d < 450)            d = 450;
    if (d > CAST_EXIT_MAX)  d = CAST_EXIT_MAX;
    return d;
}

// Telemetry only: the raw values a hardware log is judged by. Grouped here so
// the state machine above stays readable.
static void castTelemetry(int i, void *pawn, DWORD age, DWORD now)
{
    // [castwatch]: the caster's own physics through the whole window. This is
    // the ground truth for the hop - it is how the ~2.5 unit dip was found.
    if (age < 3000 && F.Location > 0 && !IsBadReadPtr(pawn, F.Location + 12)) {
        static DWORD sWAt[8] = { 0 };
        if (now - sWAt[i] >= 100) {
            sWAt[i] = now;
            float *wl = (float *)((BYTE *)pawn + F.Location);
            float wv = (F.Velocity > 0 && !IsBadReadPtr(pawn, F.Velocity + 12))
                     ? ((float *)((BYTE *)pawn + F.Velocity))[2] : 0.0f;
            BYTE wph = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 0;
            logf_("  [castwatch] p%d +%lums loc=(%.0f %.0f Z=%.1f) "
                  "vz=%.0f phys=%d", i, (unsigned long)age,
                  wl[0], wl[1], wl[2], wv, wph);
        }
    }
    // Prove the projectile actually left the caster.
    void *sa = g_castActor[i];
    if (sa && !g_castLoggedFly[i] && age >= 400 && F.Location > 0 &&
        !IsBadReadPtr(sa, F.Location + 12)) {
        float *sl = (float *)((BYTE *)sa + F.Location);
        float dx = sl[0] - g_castSpawnLoc[i][0];
        float dy = sl[1] - g_castSpawnLoc[i][1];
        float dz = sl[2] - g_castSpawnLoc[i][2];
        logf_("    [spell] p%d actor flew %.0f units in %lums "
              "(now=(%.0f %.0f %.0f))", i,
              sqrtf(dx * dx + dy * dy + dz * dz), (unsigned long)age,
              sl[0], sl[1], sl[2]);
        g_castLoggedFly[i] = TRUE;
    }
    // [yawwatch]: the actor's yaw against the view it should be facing.
    if (age >= 200 && age <= 3400 && F.Rotation > 0 &&
        !IsBadReadPtr(pawn, F.Rotation + 12)) {
        static DWORD sLastYawLog[8] = { 0 };
        if (now - sLastYawLog[i] >= 120) {
            sLastYawLog[i] = now;
            int ay = *(int *)((BYTE *)pawn + F.Rotation + 4);
            int md = meshYawDelta(pawn, ay);
            logf_("  [yawwatch] p%d +%lums actor=%d view=%d mesh=%d delta=%d",
                  i, (unsigned long)age, ay & 65535, g_viewYaw[i] & 65535,
                  (ay + md) & 65535, md);
        }
    }
}

static void finishPendingCasts(void)
{
    DWORD now = GetTickCount();
    for (int i = 1; i < 8; i++) {
        CastRun *r = &g_run[i];
        if (!r->fireAt) continue;
        void *pawn = g_pawn[i];
        DWORD age  = now - r->fireAt;

        // Ownership first: a newer cast owns the pawn from its aim onwards and
        // gets its own run, so nothing here can ever cancel a pending exit
        // (v29's rapid-cast race, re-found in v47 as the stuck-pose cause).
        if (g_beginCastAt[i] != r->beginAt) { castDropRun(i, "superseded"); continue; }
        if (!pawn || pawn != r->pawn || IsBadReadPtr(pawn, 0x200)) {
            castDropRun(i, "pawn gone/replaced"); continue;
        }

        castTelemetry(i, pawn, age, now);

        // ---- 1. EXIT ------------------------------------------------------
        if (!r->chainAt && !r->exitAt) {
            // v29: only THIS cast's own aim glow may delay the exit - a newer
            // one belongs to the next cast (and superseded us already).
            if (aimGlowAlive(i) && g_aimFXAt[i] && g_aimFXAt[i] < r->fireAt)
                continue;
            if (now - g_lastAirAt[i] < 1200) continue;   // never force mid-air
            if (age < castExitDelay(i, r)) continue;
            r->chainAt   = now;
            r->lastTryAt = now;
            castExitChain(i, pawn, r, "scheduled");
        }

        // ---- 2. VERIFY ----------------------------------------------------
        if (r->chainAt && !r->exitAt) {
            DWORD chainAge = now - r->chainAt;
            if (chainAge >= 450) {
                // StateCasting.Tick still flowing = the state never left.
                // (No tick telemetry for this cast at all - Wine stops
                // ticking at the fire - simply means "cannot verify": trust
                // the chain rather than leaving the pose parked.)
                BOOL alive = g_castTickAt[i] >= r->fireAt &&
                             (now - g_castTickAt[i] < 250);
                if (alive) {
                    if (r->retries < 2 && now - r->lastTryAt > 700 &&
                        now - g_lastAirAt[i] >= 1200) {
                        r->retries++;
                        r->lastTryAt = now;
                        logf_("    [cast] p%d run#%d StateCasting still alive "
                              "(+%lums) - exit retry %d", i, r->id,
                              (unsigned long)age, r->retries);
                        castExitChain(i, pawn, r, "retry");
                    }
                } else {
                    r->exitAt = now;
                    logf_("    [cast] p%d run#%d state LEFT (fire+%lums, "
                          "chain+%lums)", i, r->id, (unsigned long)age,
                          (unsigned long)chainAge);
                }
            }
        }

        // ---- 3. SETTLE: floor handoff runs in driveePawn, then re-pick ----
        if (r->exitAt) {
            DWORD settleAge = now - r->exitAt;
            // The anim re-pick that settles the pose and the facing. 450ms
            // after the exit the engine's own blend-out has run (v40), and it
            // waits for a quiet, standing pawn so it can never land inside
            // the exit churn. It stays armed for the rest of the run - v47's
            // version needed six gates to hold at once and expired on almost
            // every cast, which is why only the casts that happened to hop
            // came out right.
            CastGround::Sample settled = castGroundSample(i, pawn);
            if (!r->rePicked && settleAge >= 450 &&
                CastGround::readable(settled) && !settled.jumping && settled.physics <= 1 &&
                settled.vz >= -1.0f && settled.vz <= 0.0f &&
                g_p2Moving[i] == 0 && now - g_lastMoveAt[i] > 150 &&
                !aimGlowAlive(i)) {
                r->rePicked = TRUE;
                int md = (F.Rotation > 0 && !IsBadReadPtr(pawn, F.Rotation + 12))
                       ? meshYawDelta(pawn,
                             *(int *)((BYTE *)pawn + F.Rotation + 4))
                       : -99999;
                // Put the actor on the view yaw at the same moment the new
                // animation is picked, so the pose the engine chooses is the
                // one we want and not whatever the cast animation left.
                if (F.Rotation > 0 && !IsBadWritePtr((BYTE *)pawn + F.Rotation, 12)) {
                    int *pr = (int *)((BYTE *)pawn + F.Rotation);
                    pr[0] = 0; pr[1] = g_viewYaw[i]; pr[2] = 0;
                }
                if (F.DesiredRotation > 0 &&
                    !IsBadWritePtr((BYTE *)pawn + F.DesiredRotation, 12)) {
                    int *dr = (int *)((BYTE *)pawn + F.DesiredRotation);
                    dr[0] = 0; dr[1] = g_viewYaw[i]; dr[2] = 0;
                }
                if (g_fnChangeAnim)
                    callFn(pawn, g_fnChangeAnim, "ChangeAnimation [castsettle]");
                logf_("    [cast] p%d run#%d anim re-pick (exit+%lums, "
                      "meshDelta=%d)", i, r->id, (unsigned long)settleAge, md);
            }
            if (r->rePicked && settleAge >= 1200) {
                castDropRun(i, "settled");
                continue;
            }
        }

        // ---- 4. BACKSTOP + END OF RUN -------------------------------------
        if (!r->exitAt && age >= 2600 && now - g_lastAirAt[i] >= 1200) {
            // Never verified: one last exit, then let the settle stage run.
            // The pose must not be left parked. (Airborne: wait for her to
            // land - v42 showed a forced exit mid-air strands the pawn.)
            logf_("    [cast] p%d run#%d no verified exit (+%lums) - rescue",
                  i, r->id, (unsigned long)age);
            r->exitAt = now;
            castExitChain(i, pawn, r, "rescue");
        }
        if (age >= CAST_WATCH_MS) castDropRun(i, "watch end");
    }
}

// ------------------------------ aim glow (v15) ------------------------------
// The original player sees a sparkle effect at the aim point while holding
// the cast button. That effect is hgame.SpellCursorEmitter ("spell cursor" -
// the game's own aim indicator). While P2 holds cast we spawn the same
// emitter at the point the camera is aiming at and move it every frame, so
// the player can SEE where the spell will go. Destroyed on release.
static void *g_aimFX[8]   = {0};
static float g_aimDSmooth[8] = { 0 };   // v20: aim-distance hysteresis per player

// P1 cursor is read-only. A hidden SpellCursor is a controller actor, not
// dormant FX to borrow. Its owned SpellGesture/particles do the drawing.
static void *g_origCursor = NULL;
static void *findOrigCursor(void) {
    // v57: g_origCursor dies with its level; validate liveness (not just
    // readability) before asking GetFullName to walk its Outer chain.
    if (g_origCursor && cgLiveObject(g_origCursor) &&
        actorInCurrentLevel(g_origCursor) && !cgDeleted(g_origCursor)) {
        char name[160]; objName(g_origCursor,name,sizeof(name));
        if(!strncmp(name,"SpellCursor ",12)) return g_origCursor;
        // not a SpellCursor any more (slot recycled): fall through and rescan
    }
    g_origCursor = NULL;
    g_origCursor=findActorByClass("SpellCursor");
    if (g_origCursor && cgDeleted(g_origCursor)) g_origCursor=NULL;
    return g_origCursor;
}

// Prefer the cursor the live P1 controller owns. The map-wide SpellCursor is
// only a conservative fallback; a cutscene/alternate controller can own a
// different live instance, and the cooperative bridge must not drive that.
static void *findP1Cursor(void *p1)
{
    static int sOffCtrlCursor = -2;
    if (sOffCtrlCursor == -2)
        sOffCtrlCursor = propOffset("KWGame.KWHeroController.Cursor");
    if (p1 && sOffCtrlCursor > 0 && F.PawnController > 0 &&
        !IsBadReadPtr((BYTE *)p1 + F.PawnController, 4)) {
        void *ctrl = *(void **)((BYTE *)p1 + F.PawnController);
        if (ctrl && !IsBadReadPtr((BYTE *)ctrl + sOffCtrlCursor, 4)) {
            void *cursor = *(void **)((BYTE *)ctrl + sOffCtrlCursor);
            // v57: a live controller can still reference a cursor that died
            // with the previous level; require a live UObject.
            if (cursor && !IsBadReadPtr(cursor, 0x100) &&
                cgLiveObject(cursor) &&
                actorInCurrentLevel(cursor) && !cgDeleted(cursor))
                return cursor;
        }
    }
    return findOrigCursor();
}

// v23: hardware hunt for the REAL P1 aim marker. The user sees player 1's
// own marker render fine on hardware while everything we drove so far
// (bare factory emitter v20, the borrowed level SpellCursor v22) shows
// only under Wine. So: while P1 holds a real mouse AIM, tap D -> dump
// every live cursor/emitter-ish ACTOR with full renderer fields + owner.
// Whatever is visible-and-moving there, IS the original marker path.
static void diagDumpAimHunt(void)
{
    logf_("[aimhunt] === live cursor/emitter actors (D pressed) ===");
    if (!g_objArray || !F.ok || g_propOffsetField<0) { logf_("[aimhunt] waiting for calibrated live level"); return; }
    cgBuildIndex();
    static int sOffOwner = -2;
    if (sOffOwner == -2) sOffOwner = propOffset("Engine.Actor.Owner");
    char b[300], b2[160];
    int shown = 0;
    int n = g_objArray->Num;
    for (int i = 0; i < n && shown < 40; i++) {
        void *o = g_objArray->Data[i];
        if (!o || IsBadReadPtr(o, 0x100)) continue;
        objName(o, b, sizeof(b));
        // actor INSTANCE names are "ClassName Package.Path.Instance" -
        // only hunt those (metadata prints "Class ...", "Function ...",
        // "Texture ...", "Struct ...", "*Property ..." etc.)
        {
            static const char *pre[] = { "SpellCursor ", "SpellCursorEmitter ",
                                         "SpellFlyEmitter ", "SpellGesture ", "Emitter ",
                                         "HCursor ", "KWCursor ",
                                         "SelectCursor ", NULL };
            int match = 0;
            for (int p = 0; pre[p]; p++)
                if (!strncmp(b, pre[p], strlen(pre[p]))) { match = 1; break; }
            if (!match) continue;
            // class-default archetypes live inside script packages
            // ("... hgame.BeanPickup.SpriteEmitter2"); live actors live in
            // map packages ("HP3_INSIDEHUB.SpriteEmitter56", "Save0....").
            const char *path = strchr(b, ' ');
            if (path && (strncmp(path + 1, "hgame.", 6) == 0 ||
                         strncmp(path + 1, "KWGame.", 7) == 0 ||
                         strncmp(path + 1, "kwGame.", 7) == 0 ||
                         strncmp(path + 1, "Engine.", 7) == 0 ||
                         strncmp(path + 1, "HP_FX", 5) == 0)) continue;
        }
        if (!actorInCurrentLevel(o) || cgDeleted(o)) continue;
        dumpActorVisuals(o, b);
        if (sOffOwner > 0 && !IsBadReadPtr((BYTE *)o + sOffOwner, 4)) {
            void *ow = *(void **)((BYTE *)o + sOffOwner);
            logf_("      Owner=%s",
                  (ow && !IsBadReadPtr(ow, 8)) ? objName(ow, b2, sizeof(b2))
                                               : "(null)");
        }
        shown++;
    }
    logf_("[aimhunt] %d objects dumped", shown);

    // v25 section 2: the v24 hw dump found NO cursor/emitter actor that
    // could be P1's visible marker - so either it is canvas/HUD-drawn, or
    // it is named something else. Dump EVERY live drawable actor near P1
    // (bHidden=0, sprite/particle DrawType, within 2500 units of P1's
    // pawn, excluding our own aim emitters) - whatever P1's marker is, it
    // must be in THIS list while his aim is on screen.
    {
        void *p1 = g_pawn[0];
        if (!p1 || !actorInCurrentLevel(p1)) p1 = findActorByClass("harry");
        float p1l[3] = { 0, 0, 0 };
        BOOL  haveP1 = (p1 && F.Location > 0 &&
                        !IsBadReadPtr(p1, F.Location + 12));
        if (haveP1)
            memcpy(p1l, (BYTE *)p1 + F.Location, 12);
        logf_("[aimhunt2] === drawable actors near P1 (%s at %.0f %.0f %.0f) ===",
              (g_pawn[0] && g_pawnName[0][0]) ? g_pawnName[0] : "harry/p1",
              p1l[0], p1l[1], p1l[2]);
        if (!haveP1) {
            logf_("[aimhunt2] no P1 pawn - skipped");
            return;
        }
        static int  sOffHid = -2, sOffDel = -2;
        static DWORD mHid = 0, mDel = 0;
        if (sOffHid == -2) {
            sOffHid = propOffset("Engine.Actor.bHidden");
            mHid    = boolBitMask("Engine.Actor.bHidden");
            sOffDel = propOffset("Engine.Actor.bDeleteMe");
            mDel    = boolBitMask("Engine.Actor.bDeleteMe");
        }
        int drawType=propOffset("Engine.Actor.DrawType"), scale=propOffset("Engine.Actor.DrawScale"),
            style=propOffset("Engine.Actor.Style");
        if(drawType<=0 || scale<=0 || style<=0 || !g_cgChainOK) return;
        int shown2 = 0;
        for (int i2 = 0; i2 < n && shown2 < 40; i2++) {
            void *o = g_objArray->Data[i2];
            if (!o || IsBadReadPtr(o, 0x300) || !actorInCurrentLevel(o)) continue;
            CgClass *type=cgClassInfo(cgClassOf(o));
            if (!type || !type->isActor) continue;
            objName(o, b, sizeof(b));
            const char *path = strchr(b, ' ');
            if (!path) continue;                     // metadata: no instance
            if (strncmp(path + 1, "hgame.", 6) == 0 ||
                strncmp(path + 1, "KWGame.", 7) == 0 ||
                strncmp(path + 1, "kwGame.", 7) == 0 ||
                strncmp(path + 1, "Engine.", 7) == 0 ||
                strncmp(path + 1, "HP_FX", 5) == 0) continue;  // archetypes
            // metadata objects also read "Kind Package.Path" - their
            // "DrawScale"/"DrawType" land inside foreign data. A visible
            // sprite/particle actor has a real scale > 0; garbage reads
            // gave 0.00 for every metadata leak in the first hw dump.
            if (!strncmp(b, "Class ", 6) || !strncmp(b, "Function ", 9) ||
                !strncmp(b, "Texture ", 8) || !strncmp(b, "Sound ", 6) ||
                !strncmp(b, "Package ", 8) || !strncmp(b, "Struct ", 7) ||
                !strncmp(b, "State ", 6) || !strncmp(b, "Enum ", 5) ||
                !strncmp(b, "Font ", 5) || !strncmp(b, "Model ", 6) ||
                !strncmp(b, "Mesh ", 5)) continue;
            if (!strncmp(b, "SpellCursorEmitter ", 19)) continue;  // ours
            if (F.Location <= 0 || IsBadReadPtr(o, F.Location + 12)) continue;
            float *al = (float *)((BYTE *)o + F.Location);
            if (al[0] == 0.0f && al[1] == 0.0f && al[2] == 0.0f) continue;
            if (IsBadReadPtr((BYTE *)o+scale,4) || IsBadReadPtr((BYTE *)o+drawType,1) ||
                IsBadReadPtr((BYTE *)o+style,1)) continue;
            float hd = (float)sqrt((al[0]-p1l[0])*(al[0]-p1l[0]) +
                                   (al[1]-p1l[1])*(al[1]-p1l[1]));
            if (hd > 2500.0f) continue;
            if (mHid && sOffHid > 0 &&
                (*(DWORD *)((BYTE *)o + (sOffHid & ~3)) & mHid)) continue;
            if (mDel && sOffDel > 0 &&
                (*(DWORD *)((BYTE *)o + (sOffDel & ~3)) & mDel)) continue;
            BYTE dt = *(BYTE *)((BYTE *)o + drawType);
            if (dt != 1 && dt != 8 && dt != 10) continue;  // sprite/particle
            logf_("    [aimhunt2] %s  DT=%d dP1=%.0f loc=(%.0f %.0f %.0f) "
                  "style=%d scale=%.2f",
                  b, dt, hd, al[0], al[1], al[2],
                  (int)*(BYTE *)((BYTE *)o + style),
                  *(float *)((BYTE *)o + scale));
            shown2++;
        }
        logf_("[aimhunt2] %d drawable actors near P1", shown2);
        if (shown2 == 0)
            logf_("[aimhunt2] no matching visible actors in this frame; hold P1 aim and retry");
    }
}

static void nativeAimForget(int i);
static BOOL nativeAimOwnedAlive(int i);
static BOOL g_nativeAimOwned[8] = {0};
static BOOL aimGlowAlive(int i)
{
    return i >= 0 && i < 8 && g_aimFX[i] != NULL;
}

static BOOL destroyAimFX(int i)
{
    if(i<0 || i>=8) return FALSE;
    if (!g_aimFX[i]) { nativeAimForget(i); return FALSE; }
    if (g_nativeAimOwned[i] && !nativeAimOwnedAlive(i)) {
        g_aimFX[i]=NULL; nativeAimForget(i); return FALSE;
    }
    void *fx = g_aimFX[i];
    g_aimFX[i] = NULL;
    nativeAimForget(i);
    if (IsBadReadPtr(fx, 0x10)) return FALSE;
    // v57: a level change can free the emitter between the last frame and
    // this cleanup. Destroying a dead actor would crash the engine; just
    // forget it.
    if (!cgLiveObject(fx)) return FALSE;
    return destroyActorFX(fx);
}

static void aimFXDiagnose(void *pawn, void *fx, const float at[3]);

// Force the glow to render as a full-bright translucent sprite with the
// game's own sparkle texture, and breathe the scale so the marker feels
// alive. Plain sprite drawing (DrawType=1) is the same path the game's own
// cursor actor uses - it does not depend on the GameFX particle pipeline.
// v18: where SpawnSpell actually puts the spell actor, in PAWN-LOCAL space
// (learned at each fire, applied to the aim glow so the glow rides the exact
// line the spell will fly). Measured: RictusempraSpell spawns at the pawn
// ORIGIN (local 0,0,0) - the glow ray starts exactly there; other spells
// re-learn their own offset after the first cast.
static float g_spellOffLocal[3] = { 0.0f, 0.0f, 0.0f };
static BOOL  g_spellOffLearned  = FALSE;

static void rotYaw(float *v, double yawRad)
{
    float c = (float)cos(yawRad), s = (float)sin(yawRad);
    float x = v[0] * c - v[1] * s, y = v[0] * s + v[1] * c;
    v[0] = x; v[1] = y;
}

// ---------------- v31: ProcessEvent capture (E key) -----------------------
// The hop is engine-side and every chain variant is mapped (see HANDOFF);
// the missing piece is DATA: what the engine ITSELF calls when a cast ends
// naturally (player 1's own cast never hops). E taps an 8-second window in
// which UObject::ProcessEvent (Core.dll export, already resolved as
// g_ProcessEvent) is hot-patched with a logging detour. Every script event
// fired on ANY player pawn is logged (consecutive dupes collapsed). Cast
// with P1 (natural) and P2 (ours) inside the window; the log then holds
// both exit sequences for a diff. Patch is removed when the window ends.
static DWORD g_pelogEnd   = 0;
static DWORD g_pelogT0    = 0;      // v36: timestamp base (cast fire)
static int   g_pelogCount = 0;
static DWORD g_pelogLastTick[8] = {0};
static BYTE  sPEsaved[8]  = {0};
static BYTE *sPEtarget    = NULL;
static BYTE *sPEtramp     = NULL;

static void pelogStop(void)
{
    if (!g_pelogOn) return;
    g_pelogOn = FALSE;
    if (sPEtarget) {
        DWORD op = 0;
        VirtualProtect(sPEtarget, 8, PAGE_EXECUTE_READWRITE, &op);
        memcpy(sPEtarget, sPEsaved, 8);
        VirtualProtect(sPEtarget, 8, op, &op);
        FlushInstructionCache(GetCurrentProcess(), sPEtarget, 8);
    }
    logf_("[pelog] === END (%d pawn events logged) ===", g_pelogCount);
}

static void __fastcall pelogDetour(void *self, void *edx, void *func,
                                   void *parms, void *result)
{
    if (g_pelogOn) {
        if (GetTickCount() >= g_pelogEnd) {
            pelogStop();
        } else if (func && !IsBadReadPtr(func, 8) && g_pelogCount < 600) {
            static BOOL sPinged = FALSE;
            if (!sPinged) {
                sPinged = TRUE;
                logf_("[pelog] capture active - events flowing");
            }
            BOOL isPawnEv = FALSE;
            for (int p = 0; p < 8; p++) {
                if (!g_pawn[p] || g_pawn[p] != self) continue;
                isPawnEv = TRUE;
                char nb[120];
                objName(func, nb, sizeof(nb));
                // v36: timestamps since the window base (cast fire for
                // auto windows); Ticks throttled to 1/s, all else always.
                BOOL isTick = (strstr(nb, ".Tick") != NULL);
                DWORD now2 = GetTickCount();
                if (isTick) {
                    if (now2 - g_pelogLastTick[p] < 1000) break;
                    g_pelogLastTick[p] = now2;
                }
                logf_("[pelog] p%d @+%lums %s", p,
                      (unsigned long)(now2 - g_pelogT0), nb);
                g_pelogCount++;
                break;
            }
            // v51: the ACTIVATION path is not on the pawn - it runs on the
            // spell actor (ProcessTouch/HitWall/Explode) and on the object
            // it hits (HandleSpell*/OnSpellHit/Touch/Trigger/OnBounce). Log
            // those for ANY actor (P1's real cast included), so a hardware
            // log shows exactly which functions the game itself calls when
            // Harry hits a statue or a pad. Capped per window.
            if (!isPawnEv && g_cgPelogExtra < 160 && self && !IsBadReadPtr(self, 0x30)) {
                char fnb[64];
                const char *fnName = objShortName(func, fnb, sizeof(fnb));
                BOOL hit = !_strnicmp(fnName, "HandleSpell", 11) ||
                           !_stricmp(fnName, "OnHandleSpell") ||
                           ciHas(fnName, "SpellHit") || ciHas(fnName, "HitBySpell") ||
                           !_stricmp(fnName, "ProcessTouch") || !_stricmp(fnName, "Touch") ||
                           !_stricmp(fnName, "HitWall") || !_stricmp(fnName, "Explode") ||
                           !_stricmp(fnName, "Trigger") || !_stricmp(fnName, "UnTrigger") ||
                           ciHas(fnName, "Bounce") || !_stricmp(fnName, "OnSpellShutdown") ||
                           ciHas(fnName, "SpellCast") || ciHas(fnName, "CastSpell") ||
                           ciHas(fnName, "SpawnSpell") || ciHas(fnName, "ChooseSpell") ||
                           !_stricmp(fnName, "LockOn") || !_stricmp(fnName, "Activate");
                char nb[120];
                if (hit) objName(func, nb, sizeof(nb));
                if (hit && !strstr(nb, "Emitter") && !strstr(nb, "Particle")) {
                    char sb[120]; objName(self, sb, sizeof(sb));
                    if (!isReflection(sb)) {
                        char pb[80] = "";
                        // First Object parm of Touch/ProcessTouch/Trigger/
                        // HandleSpell*/OnSpellHit/OnBounce is the other actor
                        // - show it, but only when the pointer provably is an
                        // object (its Class is in the known-class set).
                        if (parms && !IsBadReadPtr(parms, 4) && g_cgNKCls > 0 &&
                            (!_stricmp(fnName, "Touch") || !_stricmp(fnName, "ProcessTouch") ||
                             !_stricmp(fnName, "Trigger") || !_strnicmp(fnName, "HandleSpell", 11) ||
                             !_stricmp(fnName, "OnSpellHit") || !_stricmp(fnName, "OnBounce"))) {
                            void *o = *(void **)parms;
                            if (o && !IsBadReadPtr(o, 0x30) && cgClassOf(o)) {
                                char ob[72]; objName(o, ob, sizeof(ob));
                                _snprintf(pb, sizeof(pb) - 1, " other=%s", ob);
                                pb[sizeof(pb) - 1] = 0;
                            }
                        }
                        logf_("[pelog] @+%lums %s :: %s%s",
                              (unsigned long)(GetTickCount() - g_pelogT0), sb, nb, pb);
                        g_pelogCount++; g_cgPelogExtra++;
                    }
                }
            }
        }
    }
    // Legacy animation-only suppression (NoDip=0). The v49 floor handoff
    // does not need this hook and never arms this window by default. Telemetry
    // and the legacy swallow still share the hook while it is installed.
    if (func && !IsBadReadPtr(func, 8)) {
        DWORD nowS = GetTickCount();
        for (int p = 0; p < 8; p++) {
            if (!g_pawn[p] || g_pawn[p] != self) continue;
            // v39: the swallow window (1.5s per chain) plus a per-cast
            // "interest" window (fire .. +4.2s) in which the state's own
            // events are harvested. Outside both: zero name lookups.
            BOOL suppress = (nowS < g_suppressLandUntil[p]);
            BOOL interest = (g_run[p].fireAt &&
                             nowS < g_run[p].fireAt + 4200);
            if (suppress || interest) {
                char nb[120];
                objName(func, nb, sizeof(nb));
                int L = (int)strlen(nb);
                if (suppress) {
                    BOOL hopAnim =
                        (L >= 12 && !strncmp(nb + L - 12,
                                             ".PlayFalling", 12)) ||
                        (L >= 21 && !strncmp(nb + L - 21,
                                             ".PlayLandingAnimation", 21));
                    if (hopAnim) {
                        logf_("[hopkill] p%d swallowed %s @+%lums", p, nb,
                              (unsigned long)(nowS - g_hopkillT0[p]));
                        return;      // swallowed - trampoline NOT called
                    }
                }
                if (interest) {
                    if (L >= 21 && !strncmp(nb + L - 21,
                                            "StateCasting.EndState", 21)) {
                        // v48: the ACTUAL exit event, and the only one that
                        // matters - it belongs to the running run (the state
                        // can only be in one cast at a time). It also covers
                        // the rare NATURAL exit: then no chain is needed at
                        // all. Capture its floor handoff before EndState runs;
                        // the next drive frame runs only after the state switch
                        // and its ground BeginState have both completed.
                        CastRun *r = &g_run[p];
                        if (r->fireAt && !r->exitAt) {
                            if (!r->chainAt) castGroundArm(p, self, r, nowS);
                            r->exitAt = nowS;
                            armHopKill(p);
                            logf_("    [cast] p%d run#%d state EXIT "
                                  "(EndState, fire+%lums)", p, r->id,
                                  (unsigned long)(nowS - r->fireAt));
                        }
                    } else if (L >= 20 && !strncmp(nb + L - 20,
                                            "StateCasting.AnimEnd", 20)) {
                        // v39 ANIMEND EXIT anchor: the cast anim's natural
                        // end (hw: release+~650ms; the state ignores it,
                        // v31). First post-release one wins.
                        if (g_animEndAt[p] < g_run[p].fireAt)
                            g_animEndAt[p] = nowS;
                    } else if (L >= 17 &&
                               !strncmp(nb + L - 17,
                                        "StateCasting.Tick", 17)) {
                        g_castTickAt[p] = nowS;   // alive = state still there
                    } else if (L >= 23 &&
                               !strncmp(nb + L - 23,
                                        "StateCasting.BeginState", 23)) {
                        g_beginCastAt[p] = nowS;  // a fresh cast began
                    } else if (L >= 30 &&
                               !strncmp(nb + L - 24,
                                        "HPCharacter.StartCasting", 24)) {
                        // StartCasting fires on EVERY cast (the aim start) -
                        // Wine skips StateCasting.BeginState about half the
                        // time, so the "a newer cast owns the pawn" test
                        // needs this 100%-reliable stamp too.
                        g_beginCastAt[p] = nowS;
                    }
                }
            }
            break;   // pawn pointers are unique: at most one p matches
        }
    }
    if (sPEtramp)
        ((PFN_ProcessEvent)sPEtramp)(self, edx, func, parms, result);
    // v43: frame-level Walking hold (see driveePawn). While a cluster is
    // armed, every ProcessEvent from the held pawn re-asserts phys=1 - the
    // detour fires many times per rendered frame, so the engine never keeps
    // a projectile tick long enough to reach the renderer.
    {
        DWORD nowH = GetTickCount();
        for (int p = 0; p < 8; p++) {
            if (!g_pawn[p] || g_pawn[p] != self) continue;
            if (g_holdArmed[p] && nowH < g_holdWalkUntil[p] &&
                F.Physics > 0 && nowH - g_lastAirAt[p] > 400 &&
                !IsBadWritePtr((BYTE *)self + F.Physics, 1) &&
                !IsBadReadPtr((BYTE *)self + F.Physics, 1)) {
                BYTE phH = *((BYTE *)self + F.Physics);
                if (phH == 5 || phH == 6)
                    *((BYTE *)self + F.Physics) = 1;
            }
            break;
        }
    }
}

// v33: O-key cast-property diff. The StateCasting loop re-pins the cast
// anim every tick because SOME condition it waits for never becomes true
// for our pawn. Every script property lives as a *Property object in the
// global object table, carrying its offset (g_propOffsetField) and, for
// bools, a power-of-two bit mask right after it. This dump lists the
// casting-relevant classes' properties and prints the ones whose VALUES
// differ between the two pawns - tap O WHILE P2 IS STUCK IN THE CAST POSE
// and the state's wait-flag (bool/byte/int) stands out against P1.
static void diagDumpCastProps(void)
{
    if (!g_objArray) { logf_("[castprop] no object table"); return; }
    void *p2 = g_pawn[1];
    void *p1 = g_pawn[0];
    if (!p1) p1 = findActorByClass("harry");
    if (!p2 || !p1) { logf_("[castprop] need both pawns (p1=%p p2=%p)",
                            p1, p2); return; }
    if (g_propOffsetField < 0) { logf_("[castprop] no prop offsets"); return; }
    logf_("[castprop] === pawn property diff (p2=%s vs p1=%s) ===",
          g_pawnName[1][0] ? g_pawnName[1] : "p2",
          g_pawnName[0][0] ? g_pawnName[0] : "p1");
    static const char *scopes[] = { ".HPCharacter.", ".KWPawn.", ".HPPawn.",
                                    ".HPHeroPawn.", ".HPCompanion", NULL };
    char b[300];
    int n = g_objArray->Num, shown = 0, scanned = 0;
    for (int i = 0; i < n && shown < 120; i++) {
        void *o = g_objArray->Data[i];
        if (!o || IsBadReadPtr(o, g_propOffsetField + 0x40)) continue;
        objName(o, b, sizeof(b));          // "BoolProperty hgame.HPCharacter.X"
        const char *sp = strchr(b, ' ');
        if (!sp) continue;
        int tlen = (int)(sp - b);
        if (tlen < 9 || strncmp(b + tlen - 8, "Property", 8) != 0) continue;
        int hit = 0;
        for (int k = 0; scopes[k]; k++)
            if (strstr(sp, scopes[k])) { hit = 1; break; }
        if (!hit) continue;
        scanned++;
        int   off = (int)*(DWORD *)((BYTE *)o + g_propOffsetField);
        if (off < 0x20 || off > 0x2000) continue;   // junk misparses
        const char *nm = strrchr(sp + 1, '.');
        nm = nm ? nm + 1 : sp + 1;
        char v2[48] = "?", v1[48] = "?";
        if      (memcmp(b, "BoolProperty", 12) == 0) {
            DWORD m = 1;
            for (int d = 4; d <= 0x38; d += 4) {
                DWORD mm = *(DWORD *)((BYTE *)o + g_propOffsetField + d);
                if (mm && (mm & (mm - 1)) == 0) { m = mm; break; }
            }
            if (off > 0 && !IsBadReadPtr(p2, off + 4) &&
                !IsBadReadPtr(p1, off + 4)) {
                BOOL a2 = (*(DWORD *)((BYTE *)p2 + off) & m) != 0;
                BOOL a1 = (*(DWORD *)((BYTE *)p1 + off) & m) != 0;
                sprintf(v2, "%d", a2); sprintf(v1, "%d", a1);
                if (a2 == a1) continue;
            } else continue;
        } else if (memcmp(b, "ByteProperty", 12) == 0 ||
                   memcmp(b, "IntProperty",  11) == 0) {
            if (off > 0 && !IsBadReadPtr(p2, off + 4) &&
                !IsBadReadPtr(p1, off + 4)) {
                int a2 = *(int *)((BYTE *)p2 + off);
                int a1 = *(int *)((BYTE *)p1 + off);
                if (a2 == a1) continue;
                sprintf(v2, "%d", a2); sprintf(v1, "%d", a1);
            } else continue;
        } else if (memcmp(b, "FloatProperty", 13) == 0) {
            if (off > 0 && !IsBadReadPtr(p2, off + 4) &&
                !IsBadReadPtr(p1, off + 4)) {
                float a2 = *(float *)((BYTE *)p2 + off);
                float a1 = *(float *)((BYTE *)p1 + off);
                if (fabsf(a2 - a1) < 0.01f) continue;
                sprintf(v2, "%.2f", a2); sprintf(v1, "%.2f", a1);
            } else continue;
        } else continue;                    // structs/objects: next round
        logf_("    [castprop] %-28s %-6s p2=%-12s p1=%-12s (+%#x)",
              nm, b[0] == 'B' ? "bool" : (b[0] == 'F' ? "float" : "int"),
              v2, v1, off);
        shown++;
    }
    logf_("[castprop] %d props scanned, %d differ", scanned, shown);
    if (scanned == 0) {
        // naming debug: show what property-ish objects actually look like
        int dbg = 0;
        for (int i = 0; i < n && dbg < 12; i++) {
            void *o = g_objArray->Data[i];
            if (!o || IsBadReadPtr(o, 0x20)) continue;
            objName(o, b, sizeof(b));
            if (!strstr(b, "HPCharacter") && !strstr(b, "hpcharacter") &&
                !strstr(b, "KWPawn")) continue;
            logf_("    [castprop-dbg] %s", b);
            dbg++;
        }
    }
    if (shown == 0)
        logf_("[castprop] NO differences - capture while she is STUCK "
              "(cast, wait 2s, then tap O)");
}

static void pelogStart(void)
{
    if (!g_ProcessEvent || g_pelogOn) return;
    if (!g_pawn[0]) g_pawn[0] = findActorByClass("harry"); // v32: the hw
    // capture had ZERO p0 events - g_pawn[0] was unresolved; the aim-hunt
    // already uses this fallback successfully.
    sPEtarget = (BYTE *)g_ProcessEvent;
    memcpy(sPEsaved, sPEtarget, 8);
    logf_("[pelog] ProcessEvent bytes: %02X %02X %02X %02X %02X %02X",
          sPEsaved[0], sPEsaved[1], sPEsaved[2],
          sPEsaved[3], sPEsaved[4], sPEsaved[5]);
    if (sPEsaved[0] == 0xE8 || sPEsaved[0] == 0xE9 || sPEsaved[0] == 0xEB) {
        logf_("[pelog] refusing to hook: relative branch in prologue");
        return;
    }
    if (!sPEtramp)
        sPEtramp = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT,
                                        PAGE_EXECUTE_READWRITE);
    if (!sPEtramp) { logf_("[pelog] trampoline alloc failed"); return; }
    // WHOLE-INSTRUCTION patch (v31 fix: the first attempt clobbered 6
    // bytes and cut "push imm32" mid-instruction -> the trampoline
    // executed garbage and hung the main thread). prologueLen() (the
    // decoder below, used by the draw hook for years) accumulates whole
    // instructions until we have >= 5 bytes; we overwrite exactly that
    // many with a jmp rel32 (x86 user space is 2GB - always in range).
    int n = prologueLen(sPEtarget);
    if (n < 5 || n > 8) {
        logf_("[pelog] refusing: prologueLen=%d", n);
        return;
    }
    logf_("[pelog] patching %d whole prologue bytes", n);
    // trampoline: saved n bytes, then jmp target+n
    memcpy(sPEtramp, sPEsaved, n);
    sPEtramp[n] = 0xE9;
    *(DWORD *)(sPEtramp + n + 1) = (DWORD)((sPEtarget + n) -
                                           (sPEtramp + n + 5));
    // patch: jmp rel32 detour (+ int3 padding)
    DWORD op = 0;
    VirtualProtect(sPEtarget, 8, PAGE_EXECUTE_READWRITE, &op);
    sPEtarget[0] = 0xE9;
    *(DWORD *)(sPEtarget + 1) = (DWORD)((BYTE *)&pelogDetour -
                                        (sPEtarget + 5));
    for (int k = 5; k < n; k++) sPEtarget[k] = 0xCC;
    VirtualProtect(sPEtarget, 8, op, &op);
    FlushInstructionCache(GetCurrentProcess(), sPEtarget, 8);
    logf_("[pelog] patch readback: %02X %02X %02X %02X %02X %02X",
          sPEtarget[0], sPEtarget[1], sPEtarget[2],
          sPEtarget[3], sPEtarget[4], sPEtarget[5]);
    g_pelogOn    = TRUE;
    g_pelogEnd   = GetTickCount() + 8000;
    g_pelogT0    = GetTickCount();
    g_pelogCount = 0;
    g_cgPelogExtra = 0;
    logf_("[pelog] === START (8s): NOW cast with P1 (mouse, natural), "
          "then with P2 (Y) ===");
}

// v36: AUTO window around every P2+ cast - no key needed. The log then
// carries the full anim/state timeline (timestamps) of each cast: when
// the natural AnimEnds land vs our exit vs the dip. Base machinery is
// the proven E-key hook; window 3.4s, t0 = the fire moment.
static void pelogAuto(void)
{
    if (!g_ProcessEvent || g_pelogOn) return;
    pelogStart();
    if (!g_pelogOn) return;
    g_pelogEnd = GetTickCount() + 6500;
    g_pelogT0  = GetTickCount() - 1200;
    logf_("[pelog] auto window: cast timeline capture (6.5s)");
}

static void aimFXVisuals(int i, BOOL init)
{
    void *fx = g_aimFX[i];
    if (!fx || IsBadWritePtr(fx, 0x200)) return;
    if (g_dbgGlowScale >= 0.0f) {          // K-key A/B probe (diagnostic)
        if (g_offDTAimFX > 0)  *(BYTE *)((BYTE *)fx + g_offDTAimFX) = 1;
        if (g_offTexAimFX > 0 && g_texAimFXFr[0])
            *(void **)((BYTE *)fx + g_offTexAimFX) = g_texAimFXFr[0];
        if (g_offStyleAimFX > 0) *(BYTE *)((BYTE *)fx + g_offStyleAimFX) = 6;
        if (g_offScaleAimFX > 0)
            *(float *)((BYTE *)fx + g_offScaleAimFX) = g_dbgGlowScale;
        return;
    }
    if (g_aimSup[i]) {                    // v24: body-clearance hidden
        if (g_offScaleAimFX > 0)
            *(float *)((BYTE *)fx + g_offScaleAimFX) = 0.001f;
        return;
    }
    // v21 rendering, driven by [actions] GlowStyle:
    //   6 (default) = the sprite path every build through v19 used - forced
    //       DrawType=1 + STY_Additive + cycling Sparkle_1/3/7 + bUnlit +
    //       pulsing DrawScale (base 1.2 * GlowSize/60). This is the ONLY
    //       variant hardware has ever shown. v20's factory particle mode
    //       (DrawType=10, Style=8, own emitter) rendered under Wine's
    //       rasterizer but was INVISIBLE on the user's hardware D3D8 path.
    //   8 = v20's factory particle mode (kept for reference/testing).
    //   other = sprite with that raw Style byte (experiment if asked).
    // Size: GlowSize% (60 default) scales the pulse base.
    if (g_glowStyle == 8) {
        if (g_offScaleAimFX > 0)
            *(float *)((BYTE *)fx + g_offScaleAimFX) =
                0.10f * (g_glowSize / 60.0f);
        if (init)
            logf_("  [aimfx] p%d visual: factory particle mode "
                  "(DrawType=10), DrawScale=%.3f", i,
                  0.10f * (g_glowSize / 60.0f));
        return;
    }
    if (g_offDTAimFX > 0)  *(BYTE *)((BYTE *)fx + g_offDTAimFX) = 1;  // DT_Sprite
    if (g_offTexAimFX > 0 && g_nAimFXFr > 0)
        *(void **)((BYTE *)fx + g_offTexAimFX) =
            g_texAimFXFr[(GetTickCount() / 120) % g_nAimFXFr];
    else if (g_offTexAimFX > 0 && g_texAimFX)
        *(void **)((BYTE *)fx + g_offTexAimFX) = g_texAimFX;
    if (g_offStyleAimFX > 0)
        *(BYTE *)((BYTE *)fx + g_offStyleAimFX) = (BYTE)g_glowStyle;
    if (g_offUnlitAimFX > 0 && g_maskUnlitAimFX) {
        DWORD *slot = (DWORD *)((BYTE *)fx + (g_offUnlitAimFX & ~3));
        *slot |= g_maskUnlitAimFX;
    }
    {
        float t = (GetTickCount() - g_aimFXAt[i]) * 0.005f;
        float s = (1.2f * g_glowSize / 60.0f) * (1.0f + 0.3f * (float)sin(t));
        if (g_offScaleAimFX > 0)
            *(float *)((BYTE *)fx + g_offScaleAimFX) = s;
        if (init)
            logf_("  [aimfx] p%d visual: sprite glow, style=%d additive, "
                  "sparkle x%d frames, DrawScale %.2f+-%.2f breathing",
                  i, g_glowStyle, g_nAimFXFr, 1.2f * g_glowSize / 60.0f,
                  0.36f * g_glowSize / 60.0f);
    }
}

#include "native_aim.h"

// ===========================================================================
// v54/v55: CO-OPERATIVE CAST HOLD FALLBACK.
//
// Some HP3 objects are not ordinary one-projectile spell targets. Their
// stock P1 route starts a shared hold: Harry's real cursor/casting state is
// visible to the companion controllers and all three heroes keep their spells
// on the object. The package class name is deliberately NOT guessed here: a
// class becomes eligible only after coopObserveP1() has observed that stock
// route - while split is OFF, P1's cursor plus both companions on the exact
// P1 cursor target; while split is ON, P1's genuine stock cursor lock (the
// v54 code never observed P1 in a live split session, so a split-ON session
// could never certify a class and a >10-second P2/P3 hold stayed a plain
// cast).
//
// A P2/P3 cast has its own camera and a mod-driven aim path, so it does not
// naturally enter that PLAYER-1-only branch. Do NOT compensate by calling
// Trigger(), by declaring every SpellTrigger complete, or by counting spell
// impacts. Instead, after an intentional uninterrupted hold on the SAME
// behaviour-verified cooperative target, mirror the exact known hold
// ingredients: the target/current-spell fields and StartCasting/playCastAim
// on the real trio, plus the real P1 SpellCursor.LockOn when its reflected ABI is safe.
// The source target's normal scripts still decide whether it completes.
//
// The timer starts over on release, aim loss, a different object, recycled
// object-table slot/class, split shutdown or level travel. It therefore cannot
// turn an ordinary quick cast (or an arbitrary Trigger) into a co-op solve.
// ===========================================================================
struct CoopCastHero {
    void *pawn;
    void *savedTarget;
    void *savedSpell;
    BOOL targetWritten;
    BOOL spellWritten;
    // v58: set when the holder RELEASED the shared hold and this borrowed
    // hero fired its one natural shot (ReleasedFire while genuinely in the
    // held cast state). Such a hero is neither StopCasting-cancelled nor
    // field-restored: its engine finalize still needs currentSpell ~300 ms.
    BOOL firedTrio;
    BOOL started;
    BOOL alreadyHolding;   // v57: the game itself recruited this hero on this
                           // target (stock companion join) - never restarted,
                           // never force-stopped on restore
};
struct CoopCastActive {
    BOOL active;
    int holder;                 // zero-based split slot that held the cast
    hp3coop::Target target;
    void *spell;
    void *cursor;
    BOOL cursorLocked;
    DWORD beganAt;
    // v58: last tick the hold's target was positively re-confirmed after
    // arming; a <=250 ms pick/cursor flicker no longer tears down the trio.
    DWORD lastSeenAt;
    char name[160];
    CoopCastHero hero[3];       // Harry, Hermione, Ron only
};
static hp3coop::Hold  g_coopHold[8] = {};
static DWORD           g_coopTryAt[8] = {0};
// A safety failure or a competing real action stays quiet until this holder
// releases or deliberately selects a new target; no 1.5 s re-hijack loop.
static BOOL            g_coopBlocked[8] = {0};
static CoopCastActive  g_coopActive = {};
static void           *g_coopCursorClass = NULL;
static void           *g_coopFnCursorLock = NULL;
static void           *g_coopFnCursorUnlock = NULL;

static BOOL coopActorAlive(void *actor)
{
    // v57: borrowed heroes and the cursor are cached for the whole hold;
    // validate them as live UObjects (a level change frees them while the
    // borrow record still names them) before any name/field access.
    return actor && !IsBadReadPtr(actor, 0x100) && cgLiveObject(actor) &&
           actorInCurrentLevel(actor) && !cgDeleted(actor);
}

static BOOL coopTargetAlive(const hp3coop::Target &target)
{
    if (!target.object || !target.clazz || target.slot < 0 || !g_objArray ||
        target.slot >= g_objArray->Num || IsBadReadPtr(g_objArray->Data,
                                                        4 * (target.slot + 1)))
        return FALSE;
    if (g_objArray->Data[target.slot] != target.object ||
        IsBadReadPtr(target.object, 0x100) || cgClassOf((void *)target.object) != target.clazz)
        return FALSE;
    return actorInCurrentLevel((void *)target.object) && !cgDeleted((void *)target.object);
}

// v58: the trio hold is re-gated to the genuine cooperative class family
// (CompanionSpellTrigger-type objects, or classes certified by the strict
// all-three observation this session). v57's any-candidate admission let a
// casual 10-second hold on a pumpkin/spawner force-borrow Ron and Hermione
// out of their AI follow behaviour mid-level. "Counts as a triple spell"
// only has meaning for the game's own cooperative cast targets anyway. This
// is a CLASS-FAMILY gate, not the v54-v56 behaviour-certification gate -
// that one proved structurally unreachable on hardware (the game's own
// companion join does not set the companions' pawn spellTarget fields, so a
// proof-needing admission could never fire).
static BOOL coopClassIsTarget(void *cls)
{
    return g_cgChainOK && cls && cgIsKnownClass(cls) && coopClassIsCooperative(cls);
}

// The hold admission is the game's own targeting - a deliberate >10-second
// hold by ANY single character on a target from the genuine cooperative
// class family. A CgCand is a real cast target by construction (its class
// carries live vulnerableToClass metadata, a spell handler, or it is a stock
// SpellTrigger-family actor), and the v58 family gate keeps ordinary objects
// from ever entering the shared trio hold.
static BOOL coopCandidateTarget(CgCand *candidate, hp3coop::Target *out)
{
    if (!candidate || !out || !g_cgChainOK || !cgCandAlive(candidate)) return FALSE;
    void *cls = candidate->info ? candidate->info->cls : cgClassOf(candidate->obj);
    if (!cls || !cgIsKnownClass(cls) || !coopClassIsCooperative(cls)) return FALSE;
    out->object = candidate->obj;
    out->clazz = cls;
    out->slot = candidate->slot;
    return TRUE;
}

// Refresh only actors whose class was proven by the unmodified P1 plus both
// AI companions. This fills the narrow gap where a legitimate cooperative
// object has no regular vulnerable-to-spell metadata, so cgScanCandidates
// deliberately omitted it. The cache is throttled because it is consulted
// only during a P2/P3 held aim and GObjObjects can be large.
static void coopRefreshCertifiedCandidates(void)
{
    DWORD now = GetTickCount();
    if (g_coopDiscoveredAt && now - g_coopDiscoveredAt < 500) return;
    g_coopDiscoveredAt = now;
    g_nCoopDiscovered = 0;
    if (!g_cgChainOK || !g_nCoopProof || !g_objArray ||
        IsBadReadPtr(g_objArray, sizeof(TArrayLite))) return;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return;
    for (int slot = 0; slot < n && g_nCoopDiscovered < COOP_DISCOVERED_MAX; slot++) {
        void *obj = g_objArray->Data[slot];
        if (!obj || IsBadReadPtr(obj, 0x100)) continue;
        void *cls = cgClassOf(obj);
        // Check the proof before allocating any class metadata: most entries
        // in GObjObjects are unrelated UObjects.
        if (!coopClassIsTarget(cls)) continue;
        CgClass *info = cgClassInfo(cls);
        if (!info || !info->isActor || info->isHero || info->isProjectile ||
            cgIsPlayerPawn(obj) || ciHas(info->token, "Cursor") ||
            ciHas(info->token, "Emitter") || !actorInCurrentLevel(obj) ||
            cgDeleted(obj) || IsBadReadPtr((BYTE *)obj + F.Location, 12)) continue;
        CgCand *candidate = &g_coopDiscovered[g_nCoopDiscovered++];
        memset(candidate, 0, sizeof(*candidate));
        candidate->obj = obj; candidate->slot = slot; candidate->info = info;
        if (info->spellClsOff > 0 &&
            !IsBadReadPtr((BYTE *)obj + info->spellClsOff, 4)) {
            void *spell = *(void **)((BYTE *)obj + info->spellClsOff);
            if (cgIsKnownClass(spell)) candidate->spellCls = spell;
        }
        if (!candidate->spellCls && cgIsKnownClass(info->handlerSpell))
            candidate->spellCls = info->handlerSpell;
        memcpy(candidate->loc, (BYTE *)obj + F.Location, 12);
        objName(obj, candidate->name, sizeof(candidate->name));
    }
}

// The normal picker is used first. Native aim intentionally rejects generic
// vulnerableToClass values, whereas a behaviour-certified cooperative target
// can legitimately accept a generic spell and choose its own shared action.
// The small fallback below uses the same camera corridor/LOS geometry, but
// considers ONLY certified cooperative targets; it does not widen ordinary
// cast targeting.
static CgCand *coopPickTarget(int i, void *pawn, hp3coop::Target *out)
{
    if (out) { out->object = NULL; out->clazz = NULL; out->slot = -1; }
    if (!g_castGameplay || !pawn || F.Location <= 0 || IsBadReadPtr(pawn, F.Location + 12))
        return NULL;

    CgCand *normal = cgPickTarget(i, pawn, NULL, FALSE, NULL);
    hp3coop::Target key = {};
    if (normal) {
        if (coopCandidateTarget(normal, &key)) {
            if (out) *out = key;
            return normal;
        }
        // A normal visible lock belongs to an ordinary object. Never search
        // through/past it for a certified target the player is not actually
        // aiming at, even after the long-hold threshold.
        return NULL;
    }

    // No normal candidate was under the reticle. Search only actors whose
    // class was behaviour-certified, preserving the same geometry below.
    coopRefreshCertifiedCandidates();
    int cy = playerCamYaw(i), cp = playerCamPitch(i);
    double ry = cy * (6.283185307179586 / 65536.0);
    double rp = cp * (6.283185307179586 / 65536.0);
    float cpr = (float)cos(rp);
    float dir[3] = { (float)cos(ry) * cpr, (float)sin(ry) * cpr, (float)sin(rp) };
    float *pl = (float *)((BYTE *)pawn + F.Location);
    float org[3] = { pl[0] + dir[0] * 90.0f, pl[1] + dir[1] * 90.0f,
                     pl[2] + dir[2] * 90.0f + 45.0f };
    float maxT = 3200.0f;
    if (g_nativeAim) {
        maxT = nativeAimRange() - 90.0f;
        if (i > 0 && i < 8 && g_aimViewValid[i]) {
            memcpy(org, g_aimViewLoc[i], 12);
            hp3aim::direction(g_aimViewRot[i], dir);
            maxT = hp3aim::rayLength(org, pl, dir, nativeAimRange());
        }
    }

    CgCand *best = NULL;
    float bestScore = 1.0e30f;
    for (int k = 0; k < g_nCoopDiscovered; k++) {
        CgCand *candidate = &g_coopDiscovered[k];
        if (!coopCandidateTarget(candidate, &key)) continue;
        float point[3]; cgAimPoint(candidate, point);
        float radius = cgHitRadius(candidate) + 90.0f;
        if (candidate->info && candidate->info->sizeOff > 0 &&
            !IsBadReadPtr((BYTE *)candidate->obj + candidate->info->sizeOff, 4)) {
            float size = *(float *)((BYTE *)candidate->obj + candidate->info->sizeOff);
            if (size >= 0.3f && size <= 4.0f) radius *= size;
        }
        if (radius < 140.0f) radius = 140.0f;
        float vx = point[0] - org[0], vy = point[1] - org[1], vz = point[2] - org[2];
        float distance = sqrtf(vx * vx + vy * vy + vz * vz);
        float along = vx * dir[0] + vy * dir[1] + vz * dir[2];
        if (along < 40.0f || along > maxT) continue;
        float perp2 = distance * distance - along * along;
        if (perp2 < 0.0f) perp2 = 0.0f;
        float perp = sqrtf(perp2);
        if (perp > radius) continue;
        if (!fastTraceClear(pawn, org, point)) {
            float back = cgHitRadius(candidate) + 10.0f;
            float front[3] = { point[0] - dir[0] * back, point[1] - dir[1] * back,
                               point[2] - dir[2] * back };
            if (!fastTraceClear(pawn, org, front)) continue;
        }
        float score = along + perp * 3.0f;
        if (score < bestScore) { bestScore = score; best = candidate; }
    }
    if (best && out) coopCandidateTarget(best, out);
    return best;
}

// Find a method on the actual P1 cursor's class chain (including a function
// declared inside one of its states), not merely on a globally similarly named
// cursor class. Nearest class wins so an override is never bypassed. Calls are
// still refused unless reflection proves the expected input ABI.
static void *coopCursorFunction(void *cursor, const char *method)
{
    if (!cursor || !method || !g_objArray || !cgIsKnownClass(cgClassOf(cursor)))
        return NULL;
    int n = g_objArray->Num;
    if (n <= 0 || n > 400000 || IsBadReadPtr(g_objArray->Data, 4)) return NULL;
    void *cls = cgClassOf(cursor);
    char classFull[260], functionFull[300];
    for (int depth = 0; cls && depth < 32; depth++, cls = cgSuper(cls)) {
        objName(cls, classFull, sizeof(classFull));
        const char *space = strchr(classFull, ' ');
        const char *classPath = space ? space + 1 : NULL;
        if (!classPath || !classPath[0]) continue;
        size_t classLen = strlen(classPath);
        void *stateMatch = NULL;
        for (int i = 0; i < n; i++) {
            void *obj = g_objArray->Data[i];
            if (!obj) continue;
            objName(obj, functionFull, sizeof(functionFull));
            if (strncmp(functionFull, "Function ", 9)) continue;
            const char *path = functionFull + 9;
            if (_strnicmp(path, classPath, classLen) || path[classLen] != '.') continue;
            const char *last = strrchr(path, '.');
            if (!last || _stricmp(last + 1, method)) continue;
            // Prefer the class declaration over same-named state handlers.
            if (!_stricmp(path + classLen + 1, method)) return obj;
            if (!stateMatch) stateMatch = obj;
        }
        if (stateMatch) return stateMatch;
    }
    return NULL;
}

static void coopResolveCursorFunctions(void *cursor)
{
    void *cursorClass = cursor ? cgClassOf(cursor) : NULL;
    if (cursorClass != g_coopCursorClass) {
        g_coopCursorClass = cursorClass;
        g_coopFnCursorLock = g_coopFnCursorUnlock = NULL;
    }
    if (!g_coopFnCursorLock)
        g_coopFnCursorLock = coopCursorFunction(cursor, "LockOn");
    if (!g_coopFnCursorUnlock) {
        g_coopFnCursorUnlock = coopCursorFunction(cursor, "UnLock");
        if (!g_coopFnCursorUnlock)
            g_coopFnCursorUnlock = coopCursorFunction(cursor, "Unlock");
    }
}

// A successful stock LockOn must leave the actual P1 cursor on the requested
// actor. ProcessEvent has no universal success return, so this live reflected
// state is the transaction's positive acknowledgement.
static BOOL coopCursorAcceptedTarget(void *cursor, void *target)
{
    static int sOffCurrent = -2;
    if (sOffCurrent == -2) {
        sOffCurrent = propOffset("KWGame.SelectCursor.aCurrentTarget");
        if (sOffCurrent < 0)
            sOffCurrent = propOffset("hgame.SpellCursor.aCurrentTarget");
    }
    if (!cursor || !target || sOffCurrent <= 0 ||
        IsBadReadPtr((BYTE *)cursor + sOffCurrent, 4)) return FALSE;
    return *(void **)((BYTE *)cursor + sOffCurrent) == target;
}

static BOOL coopCursorUnlockSafe(void)
{
    if (!g_coopFnCursorUnlock) return FALSE;
    CgFnLayout *layout = cgFnLayout(g_coopFnCursorUnlock);
    // Cleanup is part of the transaction: unlike an ordinary cgCall, do not
    // zero-fill an unknown parameter block. Only a fully reflected no-input
    // UnLock()/Unlock() (plus an optional ReturnValue) can be invoked.
    if (!layout || !layout->complete || layout->unknown) return FALSE;
    for (int k = 0; k < layout->n; k++)
        if (_stricmp(layout->p[k].name, "ReturnValue")) return FALSE;
    return TRUE;
}

static BOOL coopCallCursorLock(void *cursor, void *target)
{
    if (!cursor || !target || !g_ProcessEvent) return FALSE;
    coopResolveCursorFunctions(cursor);
    // Never acquire a cursor state that this build cannot safely release.
    if (!g_coopFnCursorLock) return FALSE;
    if (!coopCursorUnlockSafe()) {
        logf_("  [coopcast] refused SpellCursor.UnLock ABI: expected fully reflected no-input function");
        return FALSE;
    }
    CgFnLayout *layout = cgFnLayout(g_coopFnCursorLock);
    if (!layout || !layout->complete || layout->unknown) return FALSE;
    BYTE parms[256]; memset(parms, 0, sizeof(parms));
    int targetInputs = 0, otherObjectInputs = 0;
    for (int k = 0; k < layout->n; k++) {
        int off = layout->p[k].off;
        const char *name = layout->p[k].name;
        if (!_stricmp(name, "ReturnValue")) continue;
        // Script locals also appear in a UFunction's property chain. The
        // known family ABI has exactly one object input; scalar/vector locals
        // are deliberately left at their normal zero-initialized values.
        if (layout->p[k].kind != 2) continue;
        if (ciHas(name, "target")) {
            *(void **)(parms + off) = target;
            targetInputs++;
        } else {
            otherObjectInputs++;
        }
    }
    // HP2 family source has LockOn(Actor TargetActor). Refuse a different
    // object-pointer shape rather than invent a controller/caster argument.
    if (targetInputs != 1 || otherObjectInputs != 0) {
        logf_("  [coopcast] refused SpellCursor.LockOn ABI: targetObjects=%d otherObjects=%d",
              targetInputs, otherObjectInputs);
        return FALSE;
    }
    logf_("  [coopcast] ProcessEvent SpellCursor.LockOn(verified target)");
    g_ProcessEvent(cursor, NULL, g_coopFnCursorLock, parms, NULL);
    return TRUE;
}

static void coopCallCursorUnlock(void *cursor)
{
    if (!cursor || !g_ProcessEvent) return;
    coopResolveCursorFunctions(cursor);
    if (!coopCursorUnlockSafe()) {
        logf_("  [coopcast] refused SpellCursor.UnLock ABI during restore");
        return;
    }
    BYTE parms[256]; memset(parms, 0, sizeof(parms));
    logf_("  [coopcast] ProcessEvent SpellCursor.UnLock()");
    g_ProcessEvent(cursor, NULL, g_coopFnCursorUnlock, parms, NULL);
}

// v58: two endings for the armed trio hold.
//
// fireTrio == TRUE: the holder deliberately RELEASED the cast. Their own
// natural release path (the engine for P1, the mod cast-fire for P2/P3) has
// already spawned their single projectile this frame or will finalise it in
// the next few hundred ms. Every borrowed hero now RELEASES once too: one
// ReleasedFire while they are genuinely in the game's held cast state -
// exactly the stock companion release. Three near-same-frame real spells
// converge on the shared target, which is what its script counts as the
// cooperative cast. Crucially:
//   * NO StopCasting first (v57 did StopCasting then ReleasedFire, so the
//     release ran outside the cast state: the hero shouted the spell and
//     played the release animation but SpawnSpell never ran), and
//   * NO field restore on the fired heroes (the engine's StateCasting
//     finalize re-reads currentSpell at AnimEnd ~300 ms later; v57 restored
//     it to the pre-arm NULL in this same frame, so later trio fires spawned
//     nothing - the user-visible "Harry shouts but doesn't shoot"). Their
//     natural finalization consumes/resets the fields; the holder's own
//     cast state owns theirs from release onward.
// fireTrio == FALSE (target changed/deleted, another player cast, level
// travel, split shutdown): a clean silent cancel - StopCasting only, no
// ReleasedFire noise events on AI pawns (those events on an idle pawn were
// also feeding Ron's cast/follow oscillation).
static void coopRestore(const char *why, BOOL fireTrio)
{
    if (!g_coopActive.active) return;
    char targetName[180] = "<gone>";
    // This path is intentionally used for deletion/map travel too; never ask
    // GetFullName to dereference the formerly valid target in that case.
    const char *tn = targetName;
    if (coopTargetAlive(g_coopActive.target))
        tn = objName(g_coopActive.target.object, targetName, sizeof(targetName));

    // No fire against a vanished or dead target - degrade to a plain cancel.
    if (fireTrio && !coopTargetAlive(g_coopActive.target)) fireTrio = FALSE;

    if (fireTrio) {
        logf_("[coopcast] P%d RELEASED the shared hold on %s: the trio fires "
              "once (the holder's own release plus one natural companion "
              "release each - the target's own script counts this as the "
              "cooperative spell)",
              g_coopActive.holder + 1, tn);
        for (int p = 0; p < 3; p++) {
            if (p == g_coopActive.holder) continue;  // holder released on its own path
            CoopCastHero *hero = &g_coopActive.hero[p];
            // The game's own joiners are left to the game's own release.
            if (hero->alreadyHolding || !hero->started || hero->firedTrio) continue;
            if (!coopActorAlive(hero->pawn)) continue;
            hero->firedTrio = TRUE;
            if (g_fnCharRelFire)
                callFn(hero->pawn, g_fnCharRelFire,
                       "HPCharacter.ReleasedFire [coopcast trio fire]");
            logf_("  [coopcast] P%d releases its one cooperative shot at %s",
                  p + 1, tn);
        }
    } else {
        logf_("[coopcast] P%d ending shared hold on %s: %s",
              g_coopActive.holder + 1, tn, why ? why : "reset");
        for (int p = 0; p < 3; p++) {
            CoopCastHero *hero = &g_coopActive.hero[p];
            if (!coopActorAlive(hero->pawn)) continue;
            if (hero->started && !hero->firedTrio && g_fnStopCast)
                callFn(hero->pawn, g_fnStopCast, "StopCasting [coopcast cancel]");
        }
    }
    if (g_coopActive.cursorLocked && coopActorAlive(g_coopActive.cursor))
        coopCallCursorUnlock(g_coopActive.cursor);

    // Do not overwrite a state that a real controller changed while the
    // temporary bridge was active. We only put back a field while it still
    // contains the value we borrowed for this cooperative target. On the
    // RELEASE-fire ending nothing is restored at all: the fired heroes'
    // engine finalize still reads currentSpell for another few hundred ms,
    // and the holder's engine-driven cast owns its fields from release on.
    if (fireTrio) {
        memset(&g_coopActive, 0, sizeof(g_coopActive));
        return;
    }
    for (int p = 0; p < 3; p++) {
        CoopCastHero *hero = &g_coopActive.hero[p];
        if (!coopActorAlive(hero->pawn)) continue;
        if (hero->targetWritten && g_offSpellTarget > 0 &&
            !IsBadReadPtr((BYTE *)hero->pawn + g_offSpellTarget, 4) &&
            !IsBadWritePtr((BYTE *)hero->pawn + g_offSpellTarget, 4) &&
            *(void **)((BYTE *)hero->pawn + g_offSpellTarget) == g_coopActive.target.object)
            *(void **)((BYTE *)hero->pawn + g_offSpellTarget) = hero->savedTarget;
        if (hero->spellWritten && g_offCurrentSpell > 0 &&
            !IsBadReadPtr((BYTE *)hero->pawn + g_offCurrentSpell, 4) &&
            !IsBadWritePtr((BYTE *)hero->pawn + g_offCurrentSpell, 4) &&
            *(void **)((BYTE *)hero->pawn + g_offCurrentSpell) == g_coopActive.spell)
            *(void **)((BYTE *)hero->pawn + g_offCurrentSpell) = hero->savedSpell;
    }
    memset(&g_coopActive, 0, sizeof(g_coopActive));
}

// The holder owns an ordinary P2/P3 aim that predates the bridge, so normal
// restore intentionally does not stop it (its release must be allowed to fire
// once). Split shutdown is different: there will be no controlled release, so
// cancel that original hold before returning its pawn to AI.
static void coopStopHolderForSplitShutdown(void)
{
    if (!g_coopActive.active) return;
    int holder = g_coopActive.holder;
    if (holder < 0 || holder > 2) return;
    CoopCastHero *hero = &g_coopActive.hero[holder];
    if (!coopActorAlive(hero->pawn)) return;
    // v58: a plain StopCasting cancel. The ReleasedFire events after the
    // stop used to play the release shout/animation with no projectile.
    if (g_fnStopCast)
        callFn(hero->pawn, g_fnStopCast, "StopCasting [coopcast split shutdown]");
}

static void coopClearAll(const char *why)
{
    coopRestore(why, FALSE);
    for (int i = 0; i < 8; i++) {
        hp3coop::clear(g_coopHold[i]);
        g_coopTryAt[i] = 0;
        g_coopBlocked[i] = FALSE;
    }
}

// The caller snapshots savedTarget/savedSpell before invoking P1 LockOn.
// Do not re-snapshot here: LockOn itself may choose a spell, and that altered
// live value must still be restored to the pre-bridge one on cancellation.
static BOOL coopSetFields(CoopCastHero *hero, void *target, void *spell)
{
    if (!hero || !coopActorAlive(hero->pawn) || g_offSpellTarget <= 0 ||
        g_offCurrentSpell <= 0 ||
        IsBadReadPtr((BYTE *)hero->pawn + g_offSpellTarget, 4) ||
        IsBadWritePtr((BYTE *)hero->pawn + g_offSpellTarget, 4) ||
        IsBadReadPtr((BYTE *)hero->pawn + g_offCurrentSpell, 4) ||
        IsBadWritePtr((BYTE *)hero->pawn + g_offCurrentSpell, 4))
        return FALSE;
    void *liveTarget = *(void **)((BYTE *)hero->pawn + g_offSpellTarget);
    void *liveSpell = *(void **)((BYTE *)hero->pawn + g_offCurrentSpell);
    if (liveTarget != target) {
        *(void **)((BYTE *)hero->pawn + g_offSpellTarget) = target;
        hero->targetWritten = TRUE;
    }
    if (liveSpell != spell) {
        *(void **)((BYTE *)hero->pawn + g_offCurrentSpell) = spell;
        hero->spellWritten = TRUE;
        if (g_fnChoose) {
            BYTE parms[64]; memset(parms, 0, sizeof(parms));
            *(void **)(parms + 0x00) = spell;
            *(DWORD *)(parms + 0x04) = 1;
            callFnP(hero->pawn, g_fnChoose, parms, 12, "ChooseSpell [coopcast]");
        }
    }
    return TRUE;
}

static void coopBeginBorrowedHero(CoopCastHero *hero, void *spell, int p)
{
    if (!hero || !coopActorAlive(hero->pawn) || !spell) return;
    if (g_fnShowWeapon) callFn(hero->pawn, g_fnShowWeapon, "ShowWeapon [coopcast]");
    if (g_fnSwitchFight) callFn(hero->pawn, g_fnSwitchFight, "SwitchToFightMode [coopcast]");
    // v58: NO PressedFire here. On the engine-controlled pawn (Harry) an
    // unconditional PressedFire enters the genuine fire pipeline: StateCast
    // -> finalizeSpell -> SpawnSpell 150-250 ms later with no release input -
    // the user-visible "Harry auto-launches the spell as soon as the 10 s
    // hold completes". The mod's own P2 hold path provably enters and holds
    // StateCasting without PressedFire (StartCasting + playCastAim suffice),
    // so the press was both harmful and redundant.
    if (g_fnStartCast) {
        // Same ABI used by beginCast(): StartCasting(class, charge). A plain
        // zero-parameter ProcessEvent call would not select the target spell.
        BYTE parms[16]; memset(parms, 0, sizeof(parms));
        *(void **)(parms + 0x00) = spell;
        *(float *)(parms + 0x04) = 1.0f;
        callFnP(hero->pawn, g_fnStartCast, parms, 12, "StartCasting(cls,1.0) [coopcast]");
        hero->started = TRUE;
    }
    if (g_fnPlayCastAim) callFn(hero->pawn, g_fnPlayCastAim, "playCastAim [coopcast hold]");
    logf_("  [coopcast] P%d now mirrors a held cooperative spell", p + 1);
}

static void coopMaintain(void)
{
    if (!g_coopActive.active) return;
    if (!coopTargetAlive(g_coopActive.target)) {
        g_coopBlocked[g_coopActive.holder] = TRUE;
        coopRestore("target was deleted, replaced, or left this level", FALSE);
        return;
    }
    for (int p = 0; p < 3; p++) {
        CoopCastHero *hero = &g_coopActive.hero[p];
        if (!coopActorAlive(hero->pawn)) {
            g_coopBlocked[g_coopActive.holder] = TRUE;
            coopRestore("one of the trio is unavailable", FALSE);
            return;
        }
        if (hero->targetWritten && g_offSpellTarget > 0 &&
            !IsBadReadPtr((BYTE *)hero->pawn + g_offSpellTarget, 4) &&
            !IsBadWritePtr((BYTE *)hero->pawn + g_offSpellTarget, 4)) {
            void *liveTarget = *(void **)((BYTE *)hero->pawn + g_offSpellTarget);
            // A player who starts a genuine different action wins over the
            // fallback; do not keep pinning their field to our old target.
            if (p != g_coopActive.holder && p < g_numPlayers && liveTarget &&
                liveTarget != g_coopActive.target.object) {
                g_coopBlocked[g_coopActive.holder] = TRUE;
                coopRestore("another player selected a different target", FALSE);
                return;
            }
            *(void **)((BYTE *)hero->pawn + g_offSpellTarget) = g_coopActive.target.object;
        }
        if (hero->spellWritten && g_offCurrentSpell > 0 &&
            !IsBadReadPtr((BYTE *)hero->pawn + g_offCurrentSpell, 4) &&
            !IsBadWritePtr((BYTE *)hero->pawn + g_offCurrentSpell, 4)) {
            void *liveSpell = *(void **)((BYTE *)hero->pawn + g_offCurrentSpell);
            if (p != g_coopActive.holder && p < g_numPlayers && liveSpell &&
                liveSpell != g_coopActive.spell) {
                g_coopBlocked[g_coopActive.holder] = TRUE;
                coopRestore("another player selected a different spell", FALSE);
                return;
            }
            *(void **)((BYTE *)hero->pawn + g_offCurrentSpell) = g_coopActive.spell;
        }
        // v58: while a hero is borrowed by the mod and still driven by its
        // own AI controller, the controller keeps pushing movement each tick
        // while the pawn is supposed to stand and hold the cast - the
        // user-visible "Ron runs back and forth to cast together". Pin the
        // borrowed AI hero's horizontal motion for the duration of the hold
        // (human-driven heroes are left to their human, and the holder - the
        // initiating player - keeps full control).
        if (p != g_coopActive.holder && p >= g_numPlayers && hero->started &&
            !hero->alreadyHolding && F.Velocity > 0 &&
            !IsBadReadPtr((BYTE *)hero->pawn + F.Velocity, 12) &&
            !IsBadWritePtr((BYTE *)hero->pawn + F.Velocity, 12)) {
            float *vv = (float *)((BYTE *)hero->pawn + F.Velocity);
            if (vv[0] != 0.0f || vv[1] != 0.0f) { vv[0] = 0.0f; vv[1] = 0.0f; }
            if (F.Acceleration > 0 &&
                !IsBadReadPtr((BYTE *)hero->pawn + F.Acceleration, 12) &&
                !IsBadWritePtr((BYTE *)hero->pawn + F.Acceleration, 12)) {
                float *aa = (float *)((BYTE *)hero->pawn + F.Acceleration);
                if (aa[0] != 0.0f || aa[1] != 0.0f || aa[2] != 0.0f) {
                    aa[0] = 0.0f; aa[1] = 0.0f; aa[2] = 0.0f;
                }
            }
        }
    }
}

static BOOL coopStart(int holder, CgCand *candidate, const hp3coop::Target &key)
{
    // v56: holder is the zero-based split slot (0 = P1, 1 = P2, 2 = P3).
    // P1 is accepted as a holder: the engine already drives their cast
    // state, the mod only borrows the other two heroes alongside.
    if (g_coopActive.active || holder < 0 || holder > 2 || !candidate ||
        !coopTargetAlive(key)) return FALSE;
    // v58: StartCasting enters the borrowed hold, StopCasting cancels it,
    // HPCharacter.ReleasedFire is what a borrowed hero fires with when the
    // holder releases - PressedFire is deliberately NOT used any more.
    if (!g_fnCharRelFire || !g_fnStartCast || !g_fnStopCast ||
        g_offSpellTarget <= 0 || g_offCurrentSpell <= 0) {
        g_coopBlocked[holder] = TRUE;
        logf_("[coopcast] P%d cannot arm %s: casting reflection is incomplete",
              holder + 1, candidate->name);
        return FALSE;
    }

    void *fallbackSpell = g_cgBeginCls[holder];
    if (!fallbackSpell) fallbackSpell = spellClassFor(getPawn(holder));
    void *spell = cgSpellClassFor(candidate, fallbackSpell);
    if (!spell || !cgIsKnownClass(spell)) {
        g_coopBlocked[holder] = TRUE;
        logf_("[coopcast] P%d cannot arm %s: no verified target spell class",
              holder + 1, candidate->name);
        return FALSE;
    }

    CoopCastActive next = {};
    next.holder = holder;
    next.target = key;
    next.spell = spell;
    next.beganAt = GetTickCount();
    next.lastSeenAt = next.beganAt;   // v58 post-arm flicker grace anchor
    strncpy(next.name, candidate->name, sizeof(next.name) - 1);
    next.name[sizeof(next.name) - 1] = 0;

    // Snapshot first. If another real human is already aimed at a different
    // object, leave everything alone and block this held attempt. The holder
    // must release/loss the target or begin a fresh hold before retrying; AI
    // companions may be recruited normally.
    for (int p = 0; p < 3; p++) {
        CoopCastHero *hero = &next.hero[p];
        hero->pawn = getPawn(p);
        if (!coopActorAlive(hero->pawn) ||
            IsBadReadPtr((BYTE *)hero->pawn + g_offSpellTarget, 4) ||
            IsBadReadPtr((BYTE *)hero->pawn + g_offCurrentSpell, 4)) {
            g_coopBlocked[holder] = TRUE;
            logf_("[coopcast] P%d cannot arm %s: P%d is unavailable",
                  holder + 1, next.name, p + 1);
            return FALSE;
        }
        hero->savedTarget = *(void **)((BYTE *)hero->pawn + g_offSpellTarget);
        hero->savedSpell = *(void **)((BYTE *)hero->pawn + g_offCurrentSpell);
        if (p != holder && p < g_numPlayers && hero->savedTarget &&
            hero->savedTarget != key.object) {
            g_coopBlocked[holder] = TRUE;
            char busy[150] = "<unreadable target>";
            if (cgLiveObject(hero->savedTarget))
                objName(hero->savedTarget, busy, sizeof(busy));
            logf_("[coopcast] P%d leaves %s unchanged: P%d is already casting at %s",
                  holder + 1, next.name, p + 1, busy);
            return FALSE;
        }
    }

    // Commit only after every prerequisite has been checked. Keep a complete
    // restoration record before touching P1's previously avoided controller
    // path. v57: the stock P1 SpellCursor.LockOn bridge is best-effort. It is
    // the game's own "P1 is hovering this object" signal (LockOn also runs
    // ChooseSpell from the target's vulnerable class, exactly like the HP2
    // family source), but a level whose cursor exposes no safely reflected
    // LockOn/UnLock pair must no longer cancel the whole trio hold: the
    // borrowed heroes' held casts are the same effect, and the target's own
    // script still decides the result. The strict ABI checks inside
    // coopCallCursorLock are unchanged - an unsafe call is still never made.
    g_coopActive = next;
    g_coopActive.active = TRUE;
    void *cursor = findP1Cursor(g_coopActive.hero[0].pawn);
    if (coopActorAlive(cursor) && coopCallCursorLock(cursor, key.object)) {
        g_coopActive.cursor = cursor;
        g_coopActive.cursorLocked = TRUE;
        if (!coopCursorAcceptedTarget(cursor, key.object)) {
            g_coopBlocked[holder] = TRUE;
            coopRestore("stock P1 cursor rejected the requested target", FALSE);
            logf_("[coopcast] P%d left %s unchanged: P1 LockOn did not retain target",
                  holder + 1, next.name);
            return FALSE;
        }

        // LockOn can run arbitrary stock script, so validate both objects again
        // before reading a field it may have invalidated (e.g. at a level change).
        if (!coopActorAlive(g_coopActive.hero[0].pawn) || !coopTargetAlive(key) ||
            IsBadReadPtr((BYTE *)g_coopActive.hero[0].pawn + g_offSpellTarget, 4) ||
            IsBadReadPtr((BYTE *)g_coopActive.hero[0].pawn + g_offCurrentSpell, 4)) {
            g_coopBlocked[holder] = TRUE;
            coopRestore("stock P1 bridge changed the active level/state", FALSE);
            return FALSE;
        }
        // LockOn owns P1's target/spell choice. Mark either field as borrowed if
        // that stock call changed it, even when coopSetFields later finds it
        // already equal to our desired value; otherwise cancellation would leave
        // a LockOn-written P1 field behind.
        void *stockTarget = *(void **)((BYTE *)g_coopActive.hero[0].pawn + g_offSpellTarget);
        if (stockTarget != g_coopActive.hero[0].savedTarget)
            g_coopActive.hero[0].targetWritten = TRUE;
        // LockOn is the game's own spell choice for this target. Prefer it over a
        // generic P2/P3 fallback class when reflection can read a concrete class.
        void *stockSpell = *(void **)((BYTE *)g_coopActive.hero[0].pawn + g_offCurrentSpell);
        if (stockSpell && cgIsKnownClass(stockSpell) && !cgGenericSpellClass(stockSpell)) {
            spell = stockSpell;
            g_coopActive.spell = spell;
        }
        if (stockSpell != g_coopActive.hero[0].savedSpell)
            g_coopActive.hero[0].spellWritten = TRUE;
    } else {
        logf_("[coopcast] P%d arming %s without the stock P1 cursor bridge "
              "(LockOn unavailable or refused in this level); the trio hold "
              "proceeds on the borrowed heroes alone",
              holder + 1, next.name);
    }

    for (int p = 0; p < 3; p++) {
        CoopCastHero *hero = &g_coopActive.hero[p];
        // The holder gets target identity too, but is not restarted: their
        // real P2/P3 hold remains authoritative.
        if (!coopSetFields(hero, key.object, spell)) {
            g_coopBlocked[holder] = TRUE;
            coopRestore("could not write a borrowed cast field", FALSE);
            return FALSE;
        }
    }

    for (int p = 0; p < 3; p++) {
        if (p == holder) continue;
        CoopCastHero *hero = &g_coopActive.hero[p];
        // v57: a hero the game itself already put on this exact target (the
        // stock companion join - its AI entered the held cast on its own, as
        // the v56 pelog captured) is neither restarted nor force-stopped on
        // restore; the engine owns that hero's hold.
        if (hero->savedTarget == key.object &&
            (!hero->savedSpell || !spell || hero->savedSpell == spell)) {
            hero->alreadyHolding = TRUE;
            logf_("  [coopcast] P%d is already holding this target "
                  "(the game's own companion join); left untouched", p + 1);
            continue;
        }
        coopBeginBorrowedHero(hero, spell, p);
        if (!hero->started) {
            g_coopBlocked[holder] = TRUE;
            coopRestore("a borrowed hero could not enter the held cast state", FALSE);
            return FALSE;
        }
    }

    char spellName[160];
    int borrowed = 0, joined = 0;
    for (int p = 0; p < 3; p++) {
        if (p == holder) continue;
        if (g_coopActive.hero[p].alreadyHolding) joined++;
        else if (g_coopActive.hero[p].started) borrowed++;
    }
    logf_("[coopcast] P%d held %s for more than %lu seconds: the trio now holds %s on %s "
          "(%d borrowed by the mod, %d already held by the game's own companion join, "
          "cursor bridge %s)",
          holder + 1, (candidate->name[0] ? candidate->name : "co-op target"),
          (unsigned long)g_coopCastHoldMs / 1000u,
          objName(spell, spellName, sizeof(spellName)), g_coopActive.name,
          borrowed, joined, g_coopActive.cursorLocked ? "locked" : "skipped");
    return TRUE;
}

static void coopTick(int i, void *pawn, BOOL held)
{
    if (i < 0 || i > 2) return;       // Players 1..3 (0 = P1, 1 = P2, 2 = P3)
    DWORD now = GetTickCount();

    // v57: Player 1's hold is keyed by the ENGINE's own cursor lock
    // (g_p1CursorLockedTarget), not by a camera-ray pick - the stock cursor
    // is the authoritative "P1 is hovering cast on this object" signal, and
    // a ray pick can legitimately disagree with it. coopP1HoldTry owns the
    // >10s arming; this branch only maintains/restores an active P1 hold.
    if (i == 0) {
        if (!g_coopActive.active || g_coopActive.holder != 0) return;
        // v58: P1's "held" is the stock cursor lock, and the lock state
        // already carries the 250 ms None-frame grace (coopObserveP1). When
        // the lock ends the player released: the holder's engine cast fires
        // its one shot and the borrowed heroes release alongside - the trio
        // fires exactly once.
        if (!held) {
            coopRestore("holder released cast", TRUE);
            return;
        }
        if (g_p1CursorLockedTarget &&
            g_p1CursorLockedTarget == g_coopActive.target.object) {
            g_coopActive.lastSeenAt = now;
            coopMaintain();
        } else if ((DWORD)(now - g_coopActive.lastSeenAt) <= kCoopFlickerGrace) {
            coopMaintain();   // brief lock flicker after arming: keep holding
        } else {
            coopRestore("holder changed target", FALSE);
        }
        return;
    }

    hp3coop::Target key = {};
    key.slot = -1;
    CgCand *candidate = NULL;
    BOOL valid = FALSE;
    if (g_coopCastFallback && held && pawn && g_castGameplay)
        candidate = coopPickTarget(i, pawn, &key);
    if (candidate && coopCandidateTarget(candidate, &key)) valid = TRUE;

    // v57: the same 250ms flicker tolerance the P1 cursor lock gets - the
    // aim pick can drop the reticle target for a frame between updates
    // without the holder having released anything.
    hp3coop::Result state = hp3coop::update(g_coopHold[i], valid != FALSE,
                                             key, now, g_coopCastHoldMs,
                                             kCoopFlickerGrace);
    if (state == hp3coop::Started && valid) {
        // A new target is a fresh deliberate attempt after any earlier safety
        // block; it still has to earn a whole new uninterrupted interval.
        g_coopBlocked[i] = FALSE;
        logf_("[coopcast] P%d started continuous >%lu-second hold on %s",
              i + 1, (unsigned long)g_coopCastHoldMs / 1000u, candidate->name);
    }

    if (g_coopActive.active && g_coopActive.holder == i) {
        // v58: the RELEASE is the trio's fire moment, not a cancellation -
        // the holder's own normal release already fires its one shot at the
        // shared target and the borrowed heroes release one natural shot each
        // (see coopRestore). A reticle flicker (<=250 ms) while the button
        // stays held no longer tears down the armed hold; only a real target
        // change/deletion past the grace cancels silently.
        if (!held) {
            coopRestore("holder released cast", TRUE);
        } else if (valid && hp3coop::sameTarget(g_coopActive.target, key)) {
            g_coopActive.lastSeenAt = now;
            coopMaintain();
        } else if ((DWORD)(now - g_coopActive.lastSeenAt) <= kCoopFlickerGrace) {
            coopMaintain();
        } else {
            coopRestore("holder changed target", FALSE);
        }
    }
    if (!valid) {
        g_coopTryAt[i] = 0;
        g_coopBlocked[i] = FALSE;
        return;
    }
    if (g_coopActive.active || g_coopBlocked[i]) return;
    if (state == hp3coop::Ready ||
        (state == hp3coop::AlreadyReady && (DWORD)(now - g_coopTryAt[i]) >= 1500u)) {
        g_coopTryAt[i] = now;
        coopStart(i, candidate, key);
    }
}

// v56: P1's own uninterrupted >10-second cursor lock on the same object
// fires the three-character cooperative path. The cursor lock is P1's
// "holding cast on this target" signal; P1's normal cast state is already
// authoritative (the engine drives PressedFire/ReleasedFire/cast state on
// release), so the mod only has to borrow Hermione and Ron alongside. A
// short cursor flicker (<= kP1CursorGrace) is tolerated: a deliberate P1
// hold on a CompanionSpellTrigger would otherwise never satisfy this dwell
// because the stock cursor can report None for a frame or two between
// controller updates.
//
// v58: the locked object's CLASS must belong to the genuine cooperative
// family (CompanionSpellTrigger-type or session-certified). A 10-second lock
// on an ordinary castable (a pumpkin, a spawner) no longer arms the trio and
// no longer certifies its class - that self-certification was the poisoned
// gate that let any casual 10-second hold hijack Ron out of his AI routine.
static void coopP1HoldTry(void *p1)
{
    if (!g_coopCastFallback || !g_castGameplay || g_coopActive.active ||
        g_p1CursorFired || g_p1CursorLockedTarget == NULL || !p1)
        return;
    if (g_offSpellTarget <= 0) return;
    DWORD now = GetTickCount();
    if (g_p1CursorLockedAt == 0) return;
    if ((DWORD)(now - g_p1CursorLockedAt) <= g_coopCastHoldMs) return;
    // Latch so we only fire once per lock; release of the cursor resets the
    // latch (handled in coopObserveP1 when the target changes/gone).
    g_p1CursorFired = 1;
    void *target = g_p1CursorLockedTarget;
    if (!cgLiveObject(target) || !actorInCurrentLevel(target) || cgDeleted(target)) return;
    void *cls = cgClassOf(target);
    if (!cls || !cgIsKnownClass(cls)) return;
    if (!coopClassIsCooperative(cls)) {
        char tname[180];
        logf_("[coopcast] P1 >%lu-second cursor lock on %s: not the cooperative "
                "CompanionSpellTrigger class family - no trio arm (ordinary "
                "holds stay a plain single-character cast in v58)",
              (unsigned long)g_coopCastHoldMs / 1000u,
              objName(target, tname, sizeof(tname)));
        return;
    }
    // Find a CgCand for the locked object (the normal list prefers, the
    // certified list as fallback). Whichever is found, build the hp3coop
    // key from the same slot/object/class the candidate was built from.
    if (g_castGameplay) cgScanCandidates(p1, FALSE);
    CgCand *cand = cgFindCand(target);
    if (!cand) {
        coopRefreshCertifiedCandidates();
        for (int k = 0; k < g_nCoopDiscovered; k++)
            if (g_coopDiscovered[k].obj == target) { cand = &g_coopDiscovered[k]; break; }
    }
    if (!cand) {
        // No candidate found - the locked object isn't in either scan (a
        // cooperative-family actor with no vulnerable-class metadata). Build
        // an ad-hoc candidate; the class already passed the v58 family gate
        // above, so the arm itself is allowed.
        static CgCand sAdHoc = {};
        sAdHoc = {};
        sAdHoc.obj = target;
        sAdHoc.slot = -1;
        sAdHoc.info = cgClassInfo(cls);
        objName(target, sAdHoc.name, sizeof(sAdHoc.name));
        if (F.Location > 0 && !IsBadReadPtr((BYTE *)target + F.Location, 12))
            memcpy(sAdHoc.loc, (BYTE *)target + F.Location, 12);
        cand = &sAdHoc;
    }
    // Look up the slot in GObjObjects - coopTargetAlive/recoopStart both
    // require a valid slot index.
    int slot = cand->slot;
    if (slot < 0 && g_objArray) {
        int n = g_objArray->Num;
        for (int j = 0; j < n && j < 400000; j++)
            if (g_objArray->Data[j] == target) { slot = j; break; }
    }
    if (slot < 0) {
        logf_("[coopcast] P1 10s hold on %s: target not in GObjObjects - "
              "skip cooperative arming", objName(target, cand->name, sizeof(cand->name)));
        return;
    }
    hp3coop::Target key = { target, cls, slot };
    char tname[180];
    logf_("[coopcast] P1 >%lu-second hold on %s (cursor lock, split=%s): arming shared hold",
          (unsigned long)g_coopCastHoldMs / 1000u,
          objName(target, tname, sizeof(tname)), g_splitOn ? "ON" : "OFF");
    coopStart(0, cand, key);
}

// Give a real human cast priority before *any* normal input event touches its
// pawn. beginCast calls this too as a defensive fallback for future callers.
static void coopYieldToNormalCast(int i)
{
    if (g_coopActive.active && i >= 0 && i < 3) {
        g_coopBlocked[g_coopActive.holder] = TRUE;
        coopRestore("another player began a normal cast", FALSE);
    }
}

static void updateAimFX(int i, void *pawn, BOOL held,
                        const float camLoc[3], const int camRot[3])
{
    if (i < 1 || i >= 8) return;
    // v54 runs before either aim-rendering adapter. It therefore observes the
    // same held/released lifecycle even when native aim consumes this frame.
    coopTick(i, pawn, held);
    if (g_nativeAim && updateNativeAim(i,pawn,held,camLoc,camRot)) return;
    if (!held) {
        g_aimSup[i] = FALSE;              // v24: reset with the glow
        g_cgLockName[i][0] = 0;           // v51: castable lock ends with the hold
        if (g_aimFX[i]) {
            BOOL gone = destroyAimFX(i);
            logf_("  [aimfx] p%d cast released - aim glow %s", i,
                  gone ? "destroyed" : "DESTROY FAILED");
        }
        g_aimDSmooth[i] = 0.0f;   // v20: fresh hysteresis next hold
        return;
    }
    if (!pawn || !g_clsAimFX || !g_execSpawn || g_opNameConst < 0 ||
        F.Location <= 0) return;

    // Aim ray: from where the spell ACTUALLY SPAWNS, along the camera view
    // direction - the same line fireCast sends the projectile along, so the
    // glow is the spell's true flight path.
    // v20: the origin is the EXACT nudge formula from fireCast (pawnLoc +
    // camDir*90 + 45Z). v18/v19 used the learned pawn-yaw-rotated offset -
    // but when pawn and camera yaw disagree, or the aim is steeply down,
    // that origin lands at/inside her body, and with a near wall the marker
    // then SAT ON HER (v19 hardware: gold inside P2 as seen from P1, glow
    // "disappearing" when she aims at something close).
    // Engine.Actor.Trace can NOT be driven from synthesized bytecode (its
    // out-parms need the locals/ref machinery), so the aim point comes from
    // a FastTrace probe chain.
    float org[3] = { camLoc[0], camLoc[1], camLoc[2] };
    double ry = camRot[1] * (6.283185307179586 / 65536.0);
    double rp = camRot[0] * (6.283185307179586 / 65536.0);
    float cpv = (float)cos(rp);
    float dx = (float)cos(ry) * cpv, dy = (float)sin(ry) * cpv;
    float dz = (float)sin(rp);
    if (F.Location > 0 && !IsBadReadPtr(pawn, F.Location + 12)) {
        float *pl = (float *)((BYTE *)pawn + F.Location);
        org[0] = pl[0] + dx * 90.0f;
        org[1] = pl[1] + dy * 90.0f;
        org[2] = pl[2] + dz * 90.0f + 45.0f;
    }
    float aim[3] = { 0, 0, 0 };
    float aimD = 0.0f;
    BOOL snapped = FALSE;
    BOOL locked  = FALSE;

    // v51: the castable object under the spell line owns the glow, as
    // player 1's cursor locks onto the statue/pad it will fire at. Same
    // picker as the fire, so what the glow marks is what gets the spell.
    if (g_castGameplay) {
        static DWORD sCgAt[8] = { 0 };
        static void *sCgObj[8] = { 0 };
        DWORD now = GetTickCount();
        if (now - sCgAt[i] > 150) {
            sCgAt[i] = now;
            CgCand *c = cgPickTarget(i, pawn, NULL, FALSE, NULL);
            sCgObj[i] = c ? c->obj : NULL;
        }
        CgCand *c = cgFindCand(sCgObj[i]);
        if (c && cgCandAlive(c)) {
            float tp[3]; cgAimPoint(c, tp);
            if (strcmp(c->name, g_cgLockName[i])) {
                strncpy(g_cgLockName[i], c->name, sizeof(g_cgLockName[i]) - 1);
                logf_("  [aimfx] p%d castable lock -> %s", i, c->name);
            }
            aim[0] = tp[0]; aim[1] = tp[1]; aim[2] = tp[2] + 20.0f;
            snapped = TRUE; locked = TRUE;
            aimD = sqrtf((aim[0]-org[0])*(aim[0]-org[0]) + (aim[1]-org[1])*(aim[1]-org[1]) +
                         (aim[2]-org[2])*(aim[2]-org[2]));
        } else if (g_cgLockName[i][0]) {
            logf_("  [aimfx] p%d castable lock released (was %s)", i, g_cgLockName[i]);
            g_cgLockName[i][0] = 0;
        }
    }

    // AimedCast: fireCast homes the spell at the best cone target, so the
    // glow must sit on THAT actor or the two disagree again (v17 hardware
    // report: spell veered at Ron/harry while the glow marked the wall).
    // Same search, same rules - throttled, target latched between scans.
    if (g_aimedCast && !snapped) {
        static DWORD sScanAt = 0;
        static void *sLock[8] = { 0 };
        DWORD now = GetTickCount();
        if (now - sScanAt > 200) { sScanAt = now; sLock[i] = findAimTarget(pawn, NULL, FALSE); }
        void *t = sLock[i];
        if (t && !IsBadReadPtr(t, F.Location + 12)) {
            static char sLockName[8][96] = { {0} };
            char nb[96] = "";
            objName(t, nb, sizeof(nb));
            if (strcmp(nb, sLockName[i]) != 0) {
                strncpy(sLockName[i], nb, sizeof(sLockName[i]) - 1);
                float *tl = (float *)((BYTE *)t + F.Location);
                float dd = (float)sqrt((tl[0]-org[0])*(tl[0]-org[0]) +
                                       (tl[1]-org[1])*(tl[1]-org[1]) +
                                       (tl[2]-org[2])*(tl[2]-org[2]));
                logf_("  [aimfx] p%d lock -> %s at %.0f units", i, nb, dd);
            }
            float *tl = (float *)((BYTE *)t + F.Location);
            aim[0] = tl[0]; aim[1] = tl[1]; aim[2] = tl[2] + 30.0f;
            snapped = TRUE;     // already line-of-sight verified by the cone
            locked  = TRUE;
            aimD = (float)sqrt((aim[0]-org[0])*(aim[0]-org[0]) +
                               (aim[1]-org[1])*(aim[1]-org[1]) +
                               (aim[2]-org[2])*(aim[2]-org[2]));
        }
    }

    if (!snapped) {
    // v23 probe policy: fine steps from CLOSE in ({60,110,160,220,...}),
    // hug the first blocker at -40 (v20's hard 150-ray-min BURIED the
    // marker inside near walls: blockedD=150 -> want=110 -> clamped back
    // to 150 = inside the geometry, glow gone at close range), open air
    // parks at 900 (in range, not far). Hysteresis: snap IN instantly,
    // ease OUT ~35%/frame. A final clear-line validation below pulls the
    // point back until FastTrace passes - visibility beats everything.
    static const float PD[14] = { 60, 110, 160, 220, 300, 400, 520, 660,
                                  820, 1000, 1250, 1600, 2000, 3000 };
    float blockedD = 0.0f;
    for (int k = 0; k < 14; k++) {
        float q[3] = { org[0] + dx * PD[k], org[1] + dy * PD[k],
                       org[2] + dz * PD[k] };
        if (!fastTraceClear(pawn, org, q)) { blockedD = PD[k]; break; }
    }
    if (blockedD > 0.0f) snapped = TRUE;         // hugging a surface
    float want = (blockedD > 0.0f) ? blockedD - 40.0f : 900.0f;
    if (want < 60.0f) want = 60.0f;
    if (g_aimDSmooth[i] <= 0.0f || want < g_aimDSmooth[i])
        g_aimDSmooth[i] = want;
    else
        g_aimDSmooth[i] += (want - g_aimDSmooth[i]) * 0.35f;
    aimD = g_aimDSmooth[i];
    aim[0] = org[0] + dx * aimD; aim[1] = org[1] + dy * aimD;
    aim[2] = org[2] + dz * aimD;
    }
    // v22: horizontal clearance. Even at >=150 along the RAY, a steeply
    // downward aim puts the marker back under/into the caster (the ray
    // from the nudged origin re-enters her cylinder from above). Enforce
    // >=110 units of HORIZONTAL distance from the pawn origin - push the
    // point outward along the ray's horizontal projection (or her facing
    // if the ray is near-vertical).
    if (F.Location > 0 && !IsBadReadPtr(pawn, F.Location + 12)) {
        float *pc = (float *)((BYTE *)pawn + F.Location);
        float hx = aim[0] - pc[0], hy = aim[1] - pc[1];
        float hd = sqrtf(hx * hx + hy * hy);
        if (hd < 110.0f) {
            float ux, uy;
            if (fabsf(dx) + fabsf(dy) > 0.05f) { ux = dx; uy = dy; }
            else if (F.Rotation > 0 &&
                     !IsBadReadPtr(pawn, F.Rotation + 12)) {
                int fy = *(int *)((BYTE *)pawn + F.Rotation + 4);
                double fr = fy * (6.283185307179586 / 65536.0);
                ux = (float)cos(fr); uy = (float)sin(fr);
            } else { ux = 1.0f; uy = 0.0f; }
            float ul = sqrtf(ux * ux + uy * uy);
            if (ul < 0.01f) { ux = 1.0f; uy = 0.0f; ul = 1.0f; }
            float push = 110.0f - hd;
            aim[0] += ux / ul * push; aim[1] += uy / ul * push;
        }
    }
    // v23: final validation - whatever the math produced, the marker must
    // be on a CLEAR LINE from the origin (a push for clearance can land
    // inside geometry). Pull the point back toward the origin until
    // FastTrace passes; visibility beats clearance.
    for (int v = 0; v < 5; v++) {
        if (fastTraceClear(pawn, org, aim)) break;
        aim[0] = org[0] + (aim[0] - org[0]) * 0.7f;
        aim[1] = org[1] + (aim[1] - org[1]) * 0.7f;
        aim[2] = org[2] + (aim[2] - org[2]) * 0.7f;
    }
    // v24: BODY CLEARANCE - the last gate. v23 hardware log proof: close
    // blocker at d=60 + the pull-back above CONVERGED the marker onto the
    // ray origin - "placed=" landed ~30 units from the caster's own chest
    // = glow inside the character. After every other step, the point must
    // be BODY_R (horizontal) away from EVERY player pawn (caster
    // included). Try pushing along the aim's horizontal projection (open
    // ground) and sliding LEFT/RIGHT along a wall; every candidate must
    // still be FastTrace-clear. If nothing clears (nose-to-wall), HIDE
    // the glow this frame - "never inside a character" (standing rule)
    // beats "always visible". Hysteresis 95/105 stops edge flicker.
    // (BodyClear=0 in the ini skips this gate entirely - v27 isolation
    // knob for the one remaining v23->v26 delta.)
    if (g_bodyClear) {
        float need = g_aimSup[i] ? 105.0f : 95.0f;
        float ux = dx, uy = dy;
        float ul = sqrtf(ux * ux + uy * uy);
        if (ul < 0.01f) { ux = 1.0f; uy = 0.0f; ul = 1.0f; }
        ux /= ul; uy /= ul;
        float dirs[3][2] = { { ux, uy }, { -uy, ux }, { uy, -ux } };
        BOOL ok = FALSE;
        float bx = 0.0f, by = 0.0f;
        for (int dsel = 0; dsel < 3 && !ok; dsel++) {
            for (int st = 1; st <= 12 && !ok; st++) {
                float cx = aim[0] + dirs[dsel][0] * (25.0f * st);
                float cy = aim[1] + dirs[dsel][1] * (25.0f * st);
                BOOL bad = FALSE;
                for (int p2 = 0; p2 < 8; p2++) {
                    void *pp = g_pawn[p2];
                    if (!pp || F.Location <= 0 ||
                        IsBadReadPtr(pp, F.Location + 12)) continue;
                    float *pl = (float *)((BYTE *)pp + F.Location);
                    float hx = cx - pl[0], hy = cy - pl[1];
                    if (sqrtf(hx * hx + hy * hy) < need) { bad = TRUE; break; }
                }
                if (bad) continue;
                float cc[3] = { cx, cy, aim[2] };
                if (!fastTraceClear(pawn, org, cc)) continue;
                ok = TRUE; bx = cx; by = cy;
            }
        }
        if (ok) {
            if (g_aimSup[i])
                logf_("  [aimfx] p%d body-clear: resumed", i);
            g_aimSup[i] = FALSE;
            aim[0] = bx; aim[1] = by;
        } else {
            if (!g_aimSup[i])
                logf_("  [aimfx] p%d body-clear: no room (nose-to-wall) - "
                      "glow hidden", i);
            g_aimSup[i] = TRUE;
        }
    }
    if (!snapped && F.Location > 0 && !IsBadReadPtr(pawn, F.Location + 12)) {
        // unobstructed fallback: keep the glow above the caster's feet so it
        // never ends up under the floor (a buried spawn point can be blocked)
        float *pl2 = (float *)((BYTE *)pawn + F.Location);
        if (aim[2] < pl2[2] + 16.0f) aim[2] = pl2[2] + 16.0f;
    }

    // v26: remember the final aim point for the per-pane park/restore
    // (setAimFXPaneVisible) - Location writes are the ONLY per-pane
    // mechanism hardware has ever honored (see notes there).
    g_aimLast[i][0] = aim[0]; g_aimLast[i][1] = aim[1];
    g_aimLast[i][2] = aim[2];

    // Live check: freed/reused emitters respawn. bDeleteMe catches one-shot
    // FX that already finished (engine sets it before garbage collection).
    BOOL alive = FALSE;
    if (g_aimFX[i] && !IsBadReadPtr(g_aimFX[i], 0x200)) {
        if (g_offDeleteMe > 0 && g_maskDeleteMe) {
            DWORD d = *(DWORD *)((BYTE *)g_aimFX[i] + g_offDeleteMe);
            alive = ((d & g_maskDeleteMe) == 0);
        } else {
            char b[160];
            objName(g_aimFX[i], b, sizeof(b));
            alive = (strstr(b, "SpellCursor") != NULL);
        }
    }
    if (!alive) {
        if (g_aimFX[i]) destroyAimFX(i);
        // Engine.Actor.Spawn driven through a synthesized FFrame (spawnFX).
        // If the aim point itself fails placement, retry once from a point
        // straight above the caster - open sky, always placeable.
        int zeroRot[3] = { 0, 0, 0 };
        g_aimFX[i] = spawnFX(pawn, g_clsAimFX, pawn, aim, zeroRot);
        float rescue[3] = { aim[0], aim[1], aim[2] };
        BOOL rescued = FALSE;
        if (!g_aimFX[i] && F.Location > 0 && !IsBadReadPtr(pawn, F.Location + 12)) {
            float *pl3 = (float *)((BYTE *)pawn + F.Location);
            rescue[0] = pl3[0]; rescue[1] = pl3[1]; rescue[2] = pl3[2] + 150.0f;
            g_aimFX[i] = spawnFX(pawn, g_clsAimFX, pawn, rescue, zeroRot);
            rescued = TRUE;
        }
        // Read back where the engine really placed the emitter: proves the
        // token stream (class, owner, NAME_None, loc, rot) was consumed in
        // the expected order.
        float rb[3] = { 0.0f, 0.0f, 0.0f };
        if (g_aimFX[i] && F.Location > 0 &&
            !IsBadReadPtr(g_aimFX[i], F.Location + 12))
            memcpy(rb, (BYTE *)g_aimFX[i] + F.Location, 12);
        const float *req = rescued ? rescue : aim;
        logf_("  [aimfx] p%d SpellCursorEmitter req=(%.0f %.0f %.0f) d=%.0f %s "
              "placed=(%.0f %.0f %.0f)%s%s",
              i, req[0], req[1], req[2], aimD,
              locked ? "(locked)" : (snapped ? "(trace hit)" : "(fallback)"),
              rb[0], rb[1], rb[2],
              g_aimFX[i] ? "" : " SPAWN FAILED",
              rescued ? " [rescue]" : "");
        logf_("  [aimfx] p%d org=(%.0f %.0f %.0f) rot=(%d %d %d) aim=(%.0f %.0f %.0f)",
              i, org[0], org[1], org[2],
              camRot[0], camRot[1], camRot[2],
              aim[0], aim[1], aim[2]);
        if (g_aimFX[i]) {
            g_aimFXAt[i] = GetTickCount();
            aimFXVisuals(i, TRUE);
            if (g_aimDiag && !g_aimDiagDone) {
                g_aimDiagDone = TRUE;
                aimFXDiagnose(pawn, g_aimFX[i], aim);
            }
        }
    } else if (!IsBadWritePtr((BYTE *)g_aimFX[i] + F.Location, 12)) {
        float *al = (float *)((BYTE *)g_aimFX[i] + F.Location);
        al[0] = aim[0]; al[1] = aim[1]; al[2] = aim[2];
        aimFXVisuals(i, FALSE);          // keep the glow breathing
    }
}

// One-shot (dump_objects runs only): what does each candidate FX class look
// like to the renderer right after Spawn? Dumps the live aim emitter, then
// probe-spawns the other cursor/fx classes at the aim point, dumps them and
// destroys them again.
static void aimFXDiagnose(void *pawn, void *fx, const float at[3])
{
    logf_("  [aimdiag] === aim glow diagnostics ===");
    logf_("  [aimdiag] live aim emitter:");
    dumpActorVisuals(fx, "SpellCursorEmitter(live)");
    dumpCursorInstances();     // the level's own SpellCursor0 + its fields
    // v17 pose hunt: casting/aiming STATE FIELDS on the hero classes. If the
    // pawn hangs in the cast pose, something here (bCasting/bAiming-ish) is
    // still set and re-asserting the overlay every tick.
    dumpPropsMatching("Cast");
    dumpPropsMatching("Aim");
    static const char *probes[] = { "hgame.SpellFlyEmitter", "hgame.SpellCursor",
                                    NULL };
    int zr[3] = { 0, 0, 0 };
    for (int p = 0; probes[p]; p++) {
        void *cls = findObjectByPath(probes[p]);
        if (!cls) { logf_("  [aimdiag] probe %s: class not found", probes[p]); continue; }
        float loc[3] = { at[0], at[1], at[2] + 30.0f * (p + 1) };
        void *probe = spawnFX(pawn, cls, pawn, loc, zr);
        logf_("  [aimdiag] probe %s spawn -> %p", probes[p], probe);
        dumpActorVisuals(probe, probes[p]);
        if (probe) {
            BOOL gone = destroyActorFX(probe);
            logf_("  [aimdiag] probe destroyed=%d", gone);
        }
    }
    // The live emitter's particle object: what class is it and what does its
    // own config look like (texture/size decide whether anything renders)?
    char b[160];
    if (fx && !IsBadReadPtr(fx, 0x500)) {
        void **edata = *(void ***)((BYTE *)fx + 0x3F8);
        if (edata && !IsBadReadPtr(edata, 4) && edata[0] &&
            !IsBadReadPtr(edata[0], 0x100)) {
            void *pe = edata[0];
            logf_("  [aimdiag] particle object of the live emitter:");
            dumpActorVisuals(pe, "ParticleEmitter obj");
            dumpClassesMatching("SpriteEmitter");
            static const char *texPaths[] = {
                "Engine.SpriteEmitter.Texture", "KWGame.SpriteEmitter.Texture",
                "Engine.ParticleEmitter.Texture", "KWGame.ParticleEmitter.Texture",
                NULL };
            for (int t = 0; texPaths[t]; t++) {
                int o = propOffset(texPaths[t]);
                if (o < 0) continue;
                logf_("  [aimdiag] %s = +0x%X", texPaths[t], o);
                if (!IsBadReadPtr(pe, o + 4)) {
                    void *tex = *(void **)((BYTE *)pe + o);
                    logf_("    texture value = %p%s", tex,
                          (tex && !IsBadReadPtr(tex, 8)) ? objName(tex, b, sizeof(b)) : "");
                }
            }
        }
    }
    // v19: the rendered glow is the emitter's PARTICLES - the ACTOR's
    // DrawScale does not scale them (measured: identical px footprint with
    // 1.9+0.7 vs 1.2+0.4 pulsing). Dump the particle class's property list
    // and every *Size* property anywhere, so the real size knob can be
    // found and written from aimFXVisuals.
    // v19 size research, kept for the record (see the sizing note in
    // aimFXVisuals for the conclusions): the glow's fixed-size floor is
    // authored by the game FX system at spawn. StartSizeRange on the live
    // emitter and on the class-default archetype, Opacity, and the actor
    // DrawScale below ~1.6 were ALL measured inert against it.
    logf_("  [aimdiag] --- size-knob hunt (v19) ---");
    dumpClassPropsFull("hgame.SpellCursorEmitter");
    dumpPropsAny("Size");
    if (fx && !IsBadReadPtr(fx, 0x500) && g_offEmbAimFX > 0 &&
        !IsBadReadPtr((BYTE *)fx + g_offEmbAimFX, 8)) {
        void *edata = *(void **)((BYTE *)fx + g_offEmbAimFX);
        int   ecnt  = *(int *)((BYTE *)fx + g_offEmbAimFX + 4);
        logf_("    Emitters array: count=%d data=%p", ecnt, edata);
        if (ecnt < 0 || ecnt > 8) ecnt = 1;
        for (int e = 0; e < ecnt && e < 4; e++) {
            void *pe = ((void **)edata)[e];
            if (!pe || IsBadReadPtr(pe, 0x4A0)) continue;
            logf_("    emitter %d @ %p (start size range at diag time: "
                  "%.0f/%.0f)", e, pe,
                  *(float *)((BYTE *)pe + 0x2B8),
                  *(float *)((BYTE *)pe + 0x2B8 + 12));
        }
    }
    // list every SpellCursorEmitter-ish object incl. the class-default
    // emitter archetype the engine copies at spawn.
    if (g_objArray) {
        char nb[160];
        logf_("    all SpellCursorEmitter-ish objects:");
        for (int i = 0; i < g_objArray->Num; i++) {
            void *o = g_objArray->Data[i];
            if (!o || IsBadReadPtr(o, 8)) continue;
            objName(o, nb, sizeof(nb));
            if (!strstr(nb, "SpellCursorEmitter")) continue;
            logf_("      %p  %s", o, nb);
        }
    }
    logf_("  [aimdiag] === end diagnostics ===");
}

// Press: enter the same aiming/casting pose the original player uses.
// Release: actually fire. The projectile is left alive to fly and hit things.
static void beginCast(int i, void *pawn)
{
    // Defensive duplicate of the input-edge cancellation: callers outside
    // the render loop must also never overwrite a borrowed cooperative state.
    coopYieldToNormalCast(i);
    char b[160];
    void *cls = spellClassFor(pawn);
    // v51: like the wand's ChooseSpell(target.vulnerableTo...), the spell is
    // picked FROM the object under the spell line, so a statue gets its
    // Lapifors/Depulso and a pad its Spongify instead of the default spell.
    g_cgBeginCls[i] = NULL;
    if (g_castGameplay) {
        static void *sBase[8] = { 0 };     // the pawn's own spell before any override
        static void *sBasePawn[8] = { 0 };
        if (sBasePawn[i] != pawn) { sBasePawn[i] = pawn; sBase[i] = NULL; }
        if (!sBase[i]) sBase[i] = cls; else cls = sBase[i];
        CgCand *c = cgPickTarget(i, pawn, NULL, FALSE, NULL);
        void *tc = cgSpellClassFor(c, NULL);
        if (tc) {
            char tb[160];
            logf_("  [cast-begin] p%d spell chosen from target %s: %s", i,
                  c->name, objName(tc, tb, sizeof(tb)));
            cls = tc; g_cgBeginCls[i] = tc;
        }
    }
    logf_("  [cast-begin] p%d pawn=%s spellClass=%s", i, g_pawnName[i],
          cls ? objName(cls, b, sizeof(b)) : "<NONE>");
    if (!cls) { logf_("    !! no spell class available - cast skipped"); return; }

    castDropRun(i, "new aim");
    g_beginCastAt[i] = GetTickCount(); // works even with pelog stopped
    g_suppressLandUntil[i] = 0;
    if (g_offCurrentSpell > 0) *(void **)((BYTE *)pawn + g_offCurrentSpell) = cls;

    if (g_fnChoose) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        *(void **)(p + 0x00) = cls;
        *(DWORD *)(p + 0x04) = 1;
        callFnP(pawn, g_fnChoose, p, 12, "ChooseSpell(cls,force)");
    }
    if (g_fnShowWeapon)  callFn(pawn, g_fnShowWeapon, "ShowWeapon");
    if (g_fnSwitchFight) callFn(pawn, g_fnSwitchFight, "SwitchToFightStanceAnims");
    if (g_fnCanCast) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        callFnP(pawn, g_fnCanCast, p, 4, "canCast()");
    }
    if (g_fnStartCast) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        *(void **)(p + 0x00) = cls;
        *(float *)(p + 0x04) = 1.0f;
        callFnP(pawn, g_fnStartCast, p, 12, "StartCasting(cls,1.0)");
    }
    if (g_fnPlayCastAim) callFn(pawn, g_fnPlayCastAim, "playCastAim");
}

static void fireCast(int i, void *pawn)
{
    char b[160];
    void *cls = spellClassFor(pawn);
    void *target = NULL;
    void *gameTarget = NULL;
    void *spawned = NULL;
    CgCand *gameCand = NULL;

    // v51: the castable object under the camera spell line (vulnerableTo
    // class / spell handler / SpellTrigger family), chosen exactly as the
    // game's cursor chooses it. It supplies the spell class, becomes the
    // projectile's TargetActor, and gets the game's own touch when the spell
    // arrives (cgArmHit below). Straight-shot mode only applies when nothing
    // castable is on the line.
    if (g_castGameplay) {
        float gd = 0.0f;
        gameCand = cgPickTarget(i, pawn, &gd, TRUE, NULL);
        if (gameCand) {
            gameTarget = gameCand->obj;
            target = gameTarget;
            void *tc = cgSpellClassFor(gameCand, NULL);
            if (tc) cls = tc;
            else if (g_cgBeginCls[i]) cls = g_cgBeginCls[i];
        } else if (g_cgBeginCls[i]) {
            cls = g_cgBeginCls[i];    // keep what the aim phase started with
        }
        // A mature v54 bridge already proved and continuously held this exact
        // target. Preserve it through the holder's ordinary release even when
        // it is intentionally absent from g_cgCand (for example, a stock
        // co-op actor with no vulnerableToClass). This only supplies the
        // normal cast's TargetActor; it does not synthesize a hit/Trigger.
        if (g_coopActive.active && g_coopActive.holder == i &&
            coopTargetAlive(g_coopActive.target)) {
            gameCand = NULL;
            gameTarget = g_coopActive.target.object;
            target = gameTarget;
            if (g_coopActive.spell && cgIsKnownClass(g_coopActive.spell))
                cls = g_coopActive.spell;
            logf_("  [coopcast] P%d ordinary release retains shared target %s", i + 1,
                  g_coopActive.name);
        }
        if (cls && g_offCurrentSpell > 0 && !IsBadWritePtr((BYTE *)pawn + g_offCurrentSpell, 4) &&
            *(void **)((BYTE *)pawn + g_offCurrentSpell) != cls) {
            *(void **)((BYTE *)pawn + g_offCurrentSpell) = cls;
            if (g_fnChoose) {
                BYTE p[64]; memset(p, 0, sizeof(p));
                *(void **)(p + 0x00) = cls;
                *(DWORD *)(p + 0x04) = 1;
                callFnP(pawn, g_fnChoose, p, 12, "ChooseSpell(targetCls,force)");
            }
        }
    }
    logf_("  [cast-fire] p%d pawn=%s spellClass=%s", i, g_pawnName[i],
          cls ? objName(cls, b, sizeof(b)) : "<NONE>");
    if (!cls) return;

    if (!target && g_aimedCast) {
        float d = 0.0f;
        target = findAimTarget(pawn, &d, TRUE);
        if (target) logf_("    aim -> %s at %.0f units",
                          objName(target, b, sizeof(b)), d);
        else        logf_("    aim -> no target in cone, firing straight ahead");
    }
    if (g_offSpellTarget > 0) *(void **)((BYTE *)pawn + g_offSpellTarget) = target;

    // v51: aTargetOffset = the cursor's "hit point minus actor origin" - the
    // object's own targeting centre (CentreOffset), so homing aims at the
    // statue's body, not its base.
    float tgtOff[3] = { 0.0f, 0.0f, 0.0f };
    if (gameCand && cgCandAlive(gameCand)) {
        float ap[3]; cgAimPoint(gameCand, ap);
        float *tl = (float *)((BYTE *)gameCand->obj + F.Location);
        tgtOff[0] = ap[0] - tl[0]; tgtOff[1] = ap[1] - tl[1]; tgtOff[2] = ap[2] - tl[2];
    }

    if (g_fnPlayCast) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        *(void **)(p + 0x00) = target;
        *(void **)(p + 0x10) = cls;
        callFnP(pawn, g_fnPlayCast, p, 20, "playCast(tgt,0,cls)");
    }
    {   // castSpell(target, offset, class) - the gameplay fire
        BYTE p[64]; memset(p, 0, sizeof(p));
        *(void **)(p + 0x00) = target;
        memcpy(p + 0x04, tgtOff, 12);
        *(void **)(p + 0x10) = cls;
        callFnP(pawn, g_fnCast, p, 20, "castSpell(tgt,off,cls)");
    }
    if (g_fnCharFire) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        callFnP(pawn, g_fnCharFire, p, 4, "HPCharacter.Fire(0)");
    }
    if (g_fnSpawnSpell) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        *(void **)(p + 0x00) = cls;
        *(void **)(p + 0x04) = target;
        callFnP(pawn, g_fnSpawnSpell, p, 12, "SpawnSpell(cls,target)");
        spawned = *(void **)(p + 0x08);
        castRunStart(i, pawn);           // v48: one recovery run for this cast
        if (i > 0) pelogAuto();          // v36: timeline around every cast
        g_castActor[i]    = spawned;
        g_castLoggedFly[i] = FALSE;
        logf_("    SpawnSpell -> %s",
              spawned ? objName(spawned, b, sizeof(b)) : "<null>");

        // Detach from the caster and kick it along the view so it actually
        // flies like player 1's projectile instead of hanging as a glow.
        if (spawned && !IsBadReadPtr(spawned, 0x200) && F.Location > 0) {
            if (F.Base > 0 && !IsBadWritePtr((BYTE *)spawned + F.Base, 4)) {
                void *base = *(void **)((BYTE *)spawned + F.Base);
                if (base == pawn) {
                    *(void **)((BYTE *)spawned + F.Base) = NULL;
                    logf_("    spell Base cleared (was parented to caster)");
                }
            }
            // v19: SpawnSpell places the spell AT THE PAWN ORIGIN - inside
            // the caster's own ~35r/80h collision cylinder (v18 measured
            // local=(0,0,0)). From her own camera her body occludes the
            // glow, but from another player's viewing angle the additive
            // glow renders through her silhouette (v18 hardware: 255 gold
            // px inside P2's body in P1's pane), and the overlap can jolt
            // her on spawn. Nudge the actor out along the camera ray
            // BEFORE anything else reads its Location: the spawn-offset
            // learning right below then measures the NUDGED position, so
            // the aim glow's ray origin is where spells really start.
            // Keep this ABOVE the learning block and the velocity kick.
            if (!IsBadWritePtr((BYTE *)spawned + F.Location, 12) &&
                !IsBadReadPtr(pawn, F.Location + 12)) {
                int ncy = playerCamYaw(i), ncpt = playerCamPitch(i);
                double nry = ncy * (6.283185307179586 / 65536.0);
                double nrp = ncpt * (6.283185307179586 / 65536.0);
                float ncpv = (float)cos(nrp);
                float cd[3] = { (float)cos(nry) * ncpv, (float)sin(nry) * ncpv,
                                (float)sin(nrp) };
                float *npl = (float *)((BYTE *)pawn + F.Location);
                float *nsl = (float *)((BYTE *)spawned + F.Location);
                nsl[0] = npl[0] + cd[0] * 90.0f;
                nsl[1] = npl[1] + cd[1] * 90.0f;
                nsl[2] = npl[2] + cd[2] * 90.0f + 45.0f;
                logf_("    spell nudged out of caster: spawn d=(%.0f %.0f %.0f) "
                      "(camDir*90 + 45Z)",
                      nsl[0] - npl[0], nsl[1] - npl[1], nsl[2] - npl[2]);
            }
            float *sl = (float *)((BYTE *)spawned + F.Location);
            g_castSpawnLoc[i][0] = sl[0];
            g_castSpawnLoc[i][1] = sl[1];
            g_castSpawnLoc[i][2] = sl[2];
            // v18: learn where SpawnSpell really puts the spell, in pawn-local
            // space, so the aim glow's ray origin is the spell's own origin.
            if (F.Rotation > 0 && !IsBadReadPtr(pawn, F.Rotation + 12)) {
                float *pl = (float *)((BYTE *)pawn + F.Location);
                float d[3] = { sl[0] - pl[0], sl[1] - pl[1], sl[2] - pl[2] };
                int pyaw = *(int *)((BYTE *)pawn + F.Rotation + 4);
                rotYaw(d, -pyaw * (6.283185307179586 / 65536.0));
                g_spellOffLocal[0] = d[0]; g_spellOffLocal[1] = d[1];
                g_spellOffLocal[2] = d[2];
                BOOL first = !g_spellOffLearned;
                g_spellOffLearned = TRUE;
                if (first)
                    logf_("    spell spawn offset learned: local=(%.0f %.0f %.0f) "
                          "world d=(%.0f %.0f %.0f)",
                          d[0], d[1], d[2], sl[0] - pl[0], sl[1] - pl[1], sl[2] - pl[2]);
            }
            if (F.Velocity > 0) {
                float *sv = (float *)((BYTE *)spawned + F.Velocity);
                float sp = sqrtf(sv[0]*sv[0] + sv[1]*sv[1] + sv[2]*sv[2]);
                if (!target) {
                    // v13/v14: no auto-target -> fire along the camera view,
                    // yaw AND pitch ("aim where the camera points").
                    // v14: also aim the spell ACTOR's rotation and use the
                    // spell's own Speed. baseSpell extends Engine.Projectile;
                    // projectile script re-derives Velocity from Rotation, so
                    // a velocity-only kick (v13) was overwritten back to
                    // horizontal on the next tick - spells never flew up.
                    int cy = playerCamYaw(i), cpt = playerCamPitch(i);
                    double ry = cy  * (6.283185307179586 / 65536.0);
                    double rp = cpt * (6.283185307179586 / 65536.0);
                    float speed = 1200.0f;
                    if (g_offProjSpeed > 0 &&
                        !IsBadReadPtr((BYTE *)spawned + g_offProjSpeed, 4)) {
                        float s = *(float *)((BYTE *)spawned + g_offProjSpeed);
                        if (s > 100.0f && s < 20000.0f) speed = s;
                    }
                    float cpv = (float)cos(rp);
                    float dx = (float)cos(ry) * cpv, dy = (float)sin(ry) * cpv;
                    float dz = (float)sin(rp);
                    sv[0] = dx * speed; sv[1] = dy * speed; sv[2] = dz * speed;
                    if (g_offProjMaxSpeed > 0 &&
                        !IsBadWritePtr((BYTE *)spawned + g_offProjMaxSpeed, 4))
                        *(float *)((BYTE *)spawned + g_offProjMaxSpeed) = speed * 1.5f;
                    if (F.Rotation > 0 &&
                        !IsBadWritePtr((BYTE *)spawned + F.Rotation, 12)) {
                        int *sr = (int *)((BYTE *)spawned + F.Rotation);
                        sr[0] = cpt; sr[1] = cy; sr[2] = 0;
                    }
                    if (F.DesiredRotation > 0 &&
                        !IsBadWritePtr((BYTE *)spawned + F.DesiredRotation, 12)) {
                        int *dr = (int *)((BYTE *)spawned + F.DesiredRotation);
                        dr[0] = cpt; dr[1] = cy; dr[2] = 0;
                    }
                    if (F.Acceleration > 0 &&
                        !IsBadWritePtr((BYTE *)spawned + F.Acceleration, 12)) {
                        float *sa = (float *)((BYTE *)spawned + F.Acceleration);
                        sa[0] = sa[1] = sa[2] = 0.0f;   // no homing pull
                    }
                    if (F.Physics > 0)
                        *((BYTE *)spawned + F.Physics) = 6;  // PHYS_Projectile
                    logf_("    spell aimed along camera yaw=%d pitch=%d "
                          "speed=%.0f (was %.0f u/s)", cy, cpt, speed, sp);
                } else if (sp < 20.0f) {
                    // v51: a stationary spell with a gameplay target flies
                    // straight AT the target's aim point (3D), so even a
                    // non-homing spell class reaches the statue/pad. The
                    // game's own homing (TargetActor) still steers it.
                    float speed = 1200.0f;
                    if (g_offProjSpeed > 0 &&
                        !IsBadReadPtr((BYTE *)spawned + g_offProjSpeed, 4)) {
                        float s = *(float *)((BYTE *)spawned + g_offProjSpeed);
                        if (s > 100.0f && s < 20000.0f) speed = s;
                    }
                    float tp[3]; BOOL haveTP = FALSE;
                    if (gameCand && cgCandAlive(gameCand)) { cgAimPoint(gameCand, tp); haveTP = TRUE; }
                    float *sl2 = (float *)((BYTE *)spawned + F.Location);
                    float ddx = 0, ddy = 0, ddz = 0, dl = 0;
                    if (haveTP) {
                        ddx = tp[0] - sl2[0]; ddy = tp[1] - sl2[1]; ddz = tp[2] - sl2[2];
                        dl = sqrtf(ddx * ddx + ddy * ddy + ddz * ddz);
                    }
                    if (haveTP && dl > 1.0f) {
                        sv[0] = ddx / dl * speed; sv[1] = ddy / dl * speed; sv[2] = ddz / dl * speed;
                        if (F.Rotation > 0 && !IsBadWritePtr((BYTE *)spawned + F.Rotation, 12)) {
                            int *sr = (int *)((BYTE *)spawned + F.Rotation);
                            sr[0] = (int)(atan2(ddz, sqrtf(ddx * ddx + ddy * ddy)) * (65536.0 / 6.283185307179586));
                            sr[1] = (int)(atan2(ddy, ddx) * (65536.0 / 6.283185307179586));
                            sr[2] = 0;
                        }
                        logf_("    spell aimed at target point (%.0f %.0f %.0f) dist=%.0f speed=%.0f",
                              tp[0], tp[1], tp[2], dl, speed);
                    } else {
                        int yaw = playerViewYaw(i);
                        double r = yaw * (6.283185307179586 / 65536.0);
                        sv[0] = (float)cos(r) * 1200.0f;
                        sv[1] = (float)sin(r) * 1200.0f;
                        sv[2] =  40.0f;
                        logf_("    spell given projectile velocity (was stationary)");
                    }
                    if (F.Physics > 0) *((BYTE *)spawned + F.Physics) = 6; // PHYS_Projectile
                } else {
                    logf_("    spell already moving at %.0f units/s", sp);
                }
            }
        }
    }
    if (gameTarget) {
        // v51: the game's spell actor keeps TargetActor (homing) and now also
        // gets the wand's autohit delivery when it arrives - the projectile's
        // own ProcessTouch(target, hitLoc), i.e. the object's real handler.
        cgArmHit(i, pawn, spawned, gameCand, cls);
    }
    if (g_fnFinalize) callFn(pawn, g_fnFinalize, "finalizeSpell");
}

// Interact / use: trigger whatever the pawn is standing at, and pick up a
// carryable if the game says one is in range.
static void doUse(int i, void *pawn)
{
    logf_("  [use] p%d pawn=%s", i, g_pawnName[i]);
    if (g_fnCanPickup) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        callFnP(pawn, g_fnCanPickup, p, 4, "CanDoPickupActor()");
        if (p[0] && g_fnPickup) {
            BYTE q[64]; memset(q, 0, sizeof(q));
            callFnP(pawn, g_fnPickup, q, 4, "PickupActor(NULL)");
        }
    }
    if (g_fnTrigger) {
        BYTE p[64]; memset(p, 0, sizeof(p));
        callFnP(pawn, g_fnTrigger, p, 12, "Trigger()");
    }
}

// Stop the companion AI fighting us for control.
// Hard UnPossess (v7) DID stop the AI — and also stopped the engine integrating
// walking physics, so player 2 could turn and cast but not walk. The controller
// must stay linked. Soft StopTrailing + bControlAnimations=0, then while the
// player is idle we pin GroundSpeed/AccelRate to 0 and snap XY if the AI still
// shoves the pawn. While the player is moving we restore those and write vel.
static BOOL g_aiReleased[8] = {0};
static BOOL g_letAI[8]     = {0};          // debug: M key hands the pawn back
static void *g_savedCtrl[8] = {0};

// Run once even when the holder's normal input path is unavailable (for
// example after handing that pawn back to AI). This closes the same temporary
// state on control loss that updateAimFX closes on a normal cast release.
static void coopMonitor(void)
{
    if (!g_coopActive.active) return;
    int holder = g_coopActive.holder;
    if (holder < 0 || holder > 2 || g_letAI[holder] ||
        !coopActorAlive(g_coopActive.hero[holder].pawn) ||
        (g_pawn[holder] && g_pawn[holder] != g_coopActive.hero[holder].pawn)) {
        if (holder >= 0 && holder < 8) g_coopBlocked[holder] = TRUE;
        coopRestore("holder is no longer under split-screen control", FALSE);
        return;
    }
    coopMaintain();
}

// The companion follow state as it was the moment we first took the pawn, so
// that switching the split back OFF can hand the companion back to the AI in
// the same trailing configuration the game set up at level load (see aiRestore
// below). Captured once by captureFollowState(), the first time we ever touch
// the AI (in releaseAI, before any stop-trail call mutates the fields).
static BOOL  g_leashOrig[8]   = { FALSE };
static float g_saveRadius[8]  = { 0 };     // KWAIController.TrailCharFollowRadius
static void *g_saveLead[8]    = { 0 };     // KWAIController.LeadChar (Harry pawn)
static BOOL  g_saveUseDist[8] = { TRUE };  // bUseDistFromLeadCharForMovement

// Only companion/AI controllers may be UnPossessed. Calling HPAIController.UnPossess
// on a KWCutControllerII tears down the cutscene state machine and GPFs
// (AActor::ProcessState on KWCutControllerII.Scripting).
static BOOL isCompanionCtrl(const char *name)
{
    if (!name || !name[0]) return FALSE;
    if (strstr(name, "CutController") || strstr(name, "KWCut")) return FALSE;
    if (strstr(name, "HPCompanionController")) return TRUE;
    if (strstr(name, "HPAIController")) return TRUE;
    if (strstr(name, "CompanionController")) return TRUE;
    return FALSE;
}

// Record the companion AI's follow configuration THE FIRST time the mod ever
// touches a controller for player i, so that switching the split back OFF can
// hand the companion back intact via aiRestore(). This is the state the game
// set up at level load - follow radius, which lead char it trails, and
// whether it moves relative to that lead. It must run BEFORE any of the stop
// writes below change the fields, which is why releaseAI calls it before its
// script stop-trail functions.
static void captureFollowState(int i, void *ctrl)
{
    if (!ctrl || g_leashOrig[i] || g_offFollowRadius <= 0 ||
        g_offLeadChar <= 0 || g_offUseDistLead <= 0 ||
        IsBadReadPtr((BYTE *)ctrl + g_offFollowRadius, 4) ||
        IsBadReadPtr((BYTE *)ctrl + g_offLeadChar, 4))
        return;
    g_saveRadius[i]  = *(float *)((BYTE *)ctrl + g_offFollowRadius);
    g_saveLead[i]    = *(void **)((BYTE *)ctrl + g_offLeadChar);
    g_saveUseDist[i] = g_maskUseDistLead
        ? ((*(DWORD *)((BYTE *)ctrl + (g_offUseDistLead & ~3)) &
            g_maskUseDistLead) != 0) : TRUE;
    if (g_saveRadius[i] >= 0.0f && g_saveRadius[i] < 40000.0f) {
        char b[160];
        logf_("  [leash] p%d captured follow state: radius=%.0f lead=%s "
              "useDist=%d", i, g_saveRadius[i],
              g_saveLead[i] ? objName(g_saveLead[i], b, sizeof(b)) : "<none>",
              g_saveUseDist[i] ? 1 : 0);
        g_leashOrig[i] = TRUE;
    }
}

// Open the companion follow sphere (TrailCharFollowRadius) and forget the lead.
// Cheap field writes - safe every frame. The sphere around Harry is this radius.
static void cutLeash(int i, void *pawn, void *ctrl)
{
    captureFollowState(i, ctrl);   // fallback if releaseAI never captured first
    if (ctrl && g_offFollowRadius > 0 &&
        !IsBadWritePtr((BYTE *)ctrl + g_offFollowRadius, 4)) {
        float *r = (float *)((BYTE *)ctrl + g_offFollowRadius);
        if (*r < 40000.0f) {
            static DWORD lastLog[8] = {0};
            DWORD n = GetTickCount();
            if ((n - lastLog[i]) >= 2000) {
                lastLog[i] = n;
                logf_("  [leash] p%d TrailCharFollowRadius %.0f -> 50000", i, *r);
            }
            *r = 50000.0f;
        }
    }
    if (ctrl && g_offLeadChar > 0 &&
        !IsBadWritePtr((BYTE *)ctrl + g_offLeadChar, 4)) {
        void **lc = (void **)((BYTE *)ctrl + g_offLeadChar);
        if (*lc) {
            static DWORD lastLc[8] = {0};
            DWORD n = GetTickCount();
            if ((n - lastLc[i]) >= 2000) {
                lastLc[i] = n;
                char b[160];
                logf_("  [leash] p%d LeadChar %s cleared", i, objName(*lc, b, sizeof(b)));
            }
            *lc = NULL;
        }
    }
    if (ctrl && g_offUseDistLead > 0)
        setBoolProp(ctrl, g_offUseDistLead, g_maskUseDistLead, FALSE);
    // Harry.TrailingChar still pointing at us keeps the leash alive.
    void *lead = g_pawn[0];
    if (!lead) lead = findActorByClass("harry");
    if (lead) {
        if (!g_pawn[0]) g_pawn[0] = lead;
        if (g_offTrailingChar > 0 &&
            !IsBadWritePtr((BYTE *)lead + g_offTrailingChar, 4)) {
            void **tc = (void **)((BYTE *)lead + g_offTrailingChar);
            if (*tc == pawn) {
                *tc = NULL;
                logf_("  [leash] p%d dropped from lead.TrailingChar", i);
            }
        }
    }
}

static void releaseAI(int i, void *pawn, BOOL hard)
{
    void *ctrl = *(void **)((BYTE *)pawn + F.PawnController);
    char b[160];
    logf_("  [ai-release%s] p%d pawn=%s controller=%s", hard ? "/hard" : "/soft",
          i, g_pawnName[i], ctrl ? objName(ctrl, b, sizeof(b)) : "<none>");
    if (!ctrl) { logf_("    (already released)"); return; }
    if (hard && !isCompanionCtrl(b)) {
        logf_("    (not a companion AI - leaving %s alone)", b);
        return;
    }

    BYTE p[64];
    if (!hard) {
        // Capture the pristine follow state BEFORE the stop functions below
        // (or anything else) mutate the leash fields, so aiRestore() on the
        // toggle-off can hand the companion back to the same follow setup.
        captureFollowState(i, ctrl);
        if (g_fnStopTrail) { memset(p, 0, sizeof(p));
            callFnP(ctrl, g_fnStopTrail, p, 4, "OnStopTrailingLeadChar()"); }
        if (g_fnStopTrailKW) { memset(p, 0, sizeof(p));
            callFnP(ctrl, g_fnStopTrailKW, p, 4, "KWAIController.StopTrailingLeadChar()"); }
        cutLeash(i, pawn, ctrl);
        {
            void *lead = g_pawn[0] ? g_pawn[0] : findActorByClass("harry");
            if (lead && g_fnDropTrail) {
                BYTE q[64]; memset(q, 0, sizeof(q));
                *(void **)q = pawn;
                callFnP(lead, g_fnDropTrail, q, 8, "DropTrailingChar(pawn)");
            }
        }
        if (g_offCtrlAnims > 0)
            setBoolProp(ctrl, g_offCtrlAnims, g_maskCtrlAnims, FALSE);
        if (g_fnChangeAnim) callFn(pawn, g_fnChangeAnim, "ChangeAnimation");
        g_aiReleased[i] = TRUE;
        return;
    }
    if (g_offCtrlAnims > 0)
        setBoolProp(ctrl, g_offCtrlAnims, g_maskCtrlAnims, FALSE);
    g_savedCtrl[i] = ctrl;
    void *fn = g_fnUnPossessAI ? g_fnUnPossessAI : g_fnUnPossess;
    if (!fn) {
        logf_("    !! no UnPossess UFunction - falling back to soft release");
        releaseAI(i, pawn, FALSE);
        return;
    }
    memset(p, 0, sizeof(p));
    callFnP(ctrl, fn, p, 4, g_fnUnPossessAI ? "derived.UnPossess()"
                                            : "Controller.UnPossess()");
    // UnPossess may drop Physics to PHYS_None; walking physics must stay on
    // or our velocity writes are ignored and the pawn freezes.
    if (F.Physics > 0 && !IsBadWritePtr((BYTE *)pawn + F.Physics, 1)) {
        BYTE ph = *((BYTE *)pawn + F.Physics);
        if (ph == 0) *((BYTE *)pawn + F.Physics) = 1;
    }
    if (g_offDontPossess > 0)
        setBoolProp(pawn, g_offDontPossess, g_maskDontPossess, TRUE);
    // Belt: if the controller still points at us, cut that link too.
    if (F.ControllerPawn > 0 && !IsBadWritePtr((BYTE *)ctrl + F.ControllerPawn, 4)) {
        if (*(void **)((BYTE *)ctrl + F.ControllerPawn) == pawn)
            *(void **)((BYTE *)ctrl + F.ControllerPawn) = NULL;
    }
    logf_("  [ai-release/hard] Pawn.Controller now = %p phys=%d",
          *(void **)((BYTE *)pawn + F.PawnController),
          F.Physics > 0 ? *((BYTE *)pawn + F.Physics) : -1);
    g_aiReleased[i] = TRUE;
}

// Every driven frame: keep animation channels on physics, not the AI. Do NOT
// UnPossess — that kills walking. Do NOT null Controller.Pawn.
static void suppressAI(int i, void *pawn)
{
    if (!F.ok || !pawn || i < 1 || i >= 8) return;
    if (g_letAI[i]) return;
    if (F.PawnController <= 0) return;
    void *ctrl = *(void **)((BYTE *)pawn + F.PawnController);
    if (!ctrl || IsBadReadPtr(ctrl, 0x100)) return;
    char b[160];
    objName(ctrl, b, sizeof(b));
    if (!isCompanionCtrl(b)) return;
    if (g_offCtrlAnims > 0)
        setBoolProp(ctrl, g_offCtrlAnims, g_maskCtrlAnims, FALSE);
    cutLeash(i, pawn, ctrl);
}

// Undo a hard release: give the pawn back to its original controller so the
// engine resumes ticking its physics.
static void repossess(int i, void *pawn)
{
    if (!g_savedCtrl[i]) return;
    if (g_offDontPossess > 0)
        setBoolProp(pawn, g_offDontPossess, g_maskDontPossess, FALSE);
    void *fn = g_fnPossessAI ? g_fnPossessAI : findObjectByPath("Engine.Controller.Possess");
    if (!fn) return;
    BYTE p[64]; memset(p, 0, sizeof(p));
    *(void **)(p + 0x00) = pawn;                 // aPawn
    callFnP(g_savedCtrl[i], fn, p, 8, "Controller.Possess(pawn)");
    logf_("  [ai-restore] p%d Pawn.Controller now = %p", i,
          *(void **)((BYTE *)pawn + F.PawnController));
    g_savedCtrl[i] = NULL;
    g_aiReleased[i] = FALSE;
}


// ------------------------- player 2..N control -----------------------------
// Field offsets are resolved BY NAME through UProperty::Offset, never guessed.


// Canvas drawing window, resolved by name like everything else.
static struct { int OrgX, OrgY, ClipX, ClipY; } C = { -1, -1, -1, -1 };

static void resolveCanvasFields(void)
{
    if (C.ClipX >= 0 || g_propOffsetField < 0) return;
    C.OrgX  = propOffset("Engine.Canvas.OrgX");
    C.OrgY  = propOffset("Engine.Canvas.OrgY");
    C.ClipX = propOffset("Engine.Canvas.ClipX");
    C.ClipY = propOffset("Engine.Canvas.ClipY");
    logf_("[canvas] OrgX=+0x%X OrgY=+0x%X ClipX=+0x%X ClipY=+0x%X",
          C.OrgX, C.OrgY, C.ClipX, C.ClipY);
    if (C.OrgX < 0 || C.OrgY < 0 || C.ClipX < 0 || C.ClipY < 0) {
        C.ClipX = -1;                     // incomplete -> disable HUD clipping
        logf_("[canvas] incomplete - HUD will not be clipped");
    }
}

static void resolveFields(void)
{
    static int tries = 0;
    if (F.ok || tries >= 5) return;
    tries++;
    if (g_propOffsetField < 0) calibratePropertyOffset();
    if (g_propOffsetField < 0) return;
    F.Location       = propOffset("Engine.Actor.Location");
    F.Rotation       = propOffset("Engine.Actor.Rotation");
    F.Velocity       = propOffset("Engine.Actor.Velocity");
    F.Acceleration   = propOffset("Engine.Actor.Acceleration");
    F.LifeSpan       = propOffset("Engine.Actor.LifeSpan");
    F.Physics        = propOffset("Engine.Actor.Physics");
    F.PawnController = propOffset("Engine.Pawn.Controller");
    F.ControllerPawn = propOffset("Engine.Controller.Pawn");
    F.GroundSpeed    = propOffset("Engine.Pawn.GroundSpeed");
    F.JumpZ          = propOffset("Engine.Pawn.JumpZ");
    F.AccelRate      = propOffset("Engine.Pawn.AccelRate");
    F.Base           = propOffset("Engine.Actor.Base");
    F.Floor          = propOffset("Engine.Pawn.Floor");
    F.DesiredRotation= propOffset("Engine.Actor.DesiredRotation");
    F.RotationRate   = propOffset("Engine.Actor.RotationRate");
    resolveCanvasFields();
    F.ok = (F.Location > 0 && F.Rotation > 0 && F.Acceleration > 0 &&
            F.PawnController > 0 && F.ControllerPawn > 0);
    logf_("[fields] Loc=+0x%X Rot=+0x%X Vel=+0x%X Accel=+0x%X Phys=+0x%X "
          "Pawn.Ctrl=+0x%X Ctrl.Pawn=+0x%X GroundSpeed=+0x%X JumpZ=+0x%X "
          "AccelRate=+0x%X Base=+0x%X DesRot=+0x%X RotRate=+0x%X -> %s",
          F.Location, F.Rotation, F.Velocity, F.Acceleration, F.Physics,
          F.PawnController, F.ControllerPawn, F.GroundSpeed, F.JumpZ,
          F.AccelRate, F.Base, F.DesiredRotation, F.RotationRate,
          F.ok ? "OK" : "INCOMPLETE");
    logf_("[fields] Floor=+0x%X (v49 native floor handoff %s)", F.Floor,
          F.Floor > 0 && F.Base > 0 && g_execSetPhysics && g_opByteConst >= 0
              ? "available" : "unavailable - engine gravity unchanged");
    // v41: the pawn's mesh COMPONENT carries its own rotation (UE2
    // Actor-derived object). A mesh yaw stuck away from the actor yaw is
    // the visible "standing in incorrect yaw". Resolved by name like every
    // other offset; <=0 just disables the v41 yaw watch/fix.
    g_meshOff = (g_propOffsetField >= 0)
              ? propOffset("Engine.Actor.Mesh") : -1;
    logf_("[fields] Mesh component prop=+0x%X (v41 yaw guard %s)",
          g_meshOff, g_meshOff > 0 ? "ARMED" : "off");
}

// Take authority over a pawn. Soft StopTrailing is not enough (AI Tick writes
// Acceleration before our PostRender zero). Default is a derived UnPossess,
// then we keep PHYS_Walking so collision/gravity/walk-anims still work.
static BOOL g_detached[8] = {0};
static float g_savedGS[8]  = {0};
static float g_savedAR[8]  = {0};
static float g_pinXY[8][2] = {{0}};
static BOOL  g_pinned[8]   = {0};
static void takeControl(int i, void *pawn)
{
    if (!F.ok || F.Physics < 0) return;
    if (!pawn || IsBadReadPtr(pawn, F.Physics + 1)) return;
    if (g_letAI[i]) return;                // debug repossess - leave AI alone
    resolveActions();
    if (!g_detached[i]) {
        BYTE *phys = (BYTE *)pawn + F.Physics;
        char b[160];
        void *ctrl = (F.PawnController > 0) ? *(void **)((BYTE *)pawn + F.PawnController) : NULL;
        logf_("  player %d takes control of %s (was Physics=%d, AI=%s)",
              i, g_pawnName[i], *phys,
              ctrl ? objName(ctrl, b, sizeof(b)) : "<none>");
        g_detached[i] = TRUE;
        releaseAI(i, pawn, FALSE);         // soft - keep the controller for physics
        if (g_fnChangeAnim) callFn(pawn, g_fnChangeAnim, "ChangeAnimation");
    }
    suppressAI(i, pawn);                   // every frame, in case it re-binds
}

// Hand a driven companion pawn back to its companion AI once the split is
// switched OFF. This reverses everything takeControl/releaseAI/suppressAI and
// driveePawn did to the pawn and its controller:
//   * give the controller back animation control (bControlAnimations=1);
//   * restore the trailing configuration captured in cutLeash at take-over
//     (LeadChar, TrailCharFollowRadius, bUseDistFromLeadCharForMovement and
//     the lead's TrailingChar back-pointer) so the AI keeps following Harry
//     the same way the game set it up;
//   * if a hard release had UnPossessed the controller, reattach it via
//     Controller.Possess;
//   * un-freeze walking physics and restore GroundSpeed/AccelRate that the
//     idle driver had pinned to 0, so the AI's own Tick owns the pawn again;
//   * clear the per-view "we own this pawn" state so re-enabling the split
//     later re-takes control cleanly.
static void aiRestore(int i, void *pawn)
{
    if (!F.ok || !pawn || i < 1 || i >= 8) return;
    if (!g_detached[i] && !g_aiReleased[i] && !g_savedCtrl[i]) return;
    if (g_letAI[i]) return;                // debug M already handed it back
    resolveActions();
    char b[160];
    void *ctrl = (F.PawnController > 0 &&
                  !IsBadReadPtr((BYTE *)pawn + F.PawnController, 4))
               ? *(void **)((BYTE *)pawn + F.PawnController) : NULL;
    logf_("  [ai-restore] p%d pawn=%s controller=%s", i,
          g_pawnName[i][0] ? g_pawnName[i] : "?",
          ctrl ? objName(ctrl, b, sizeof(b)) : "<none>");

    // A HARD release UnPossessed the controller (Pawn.Controller was cut).
    // Reattach it with Controller.Possess so the AI owns the pawn again.
    if (!ctrl && g_savedCtrl[i]) {
        if (g_offDontPossess > 0)
            setBoolProp(pawn, g_offDontPossess, g_maskDontPossess, FALSE);
        void *fn = g_fnPossessAI ? g_fnPossessAI
                                 : findObjectByPath("Engine.Controller.Possess");
        if (fn) {
            BYTE p[64]; memset(p, 0, sizeof(p));
            *(void **)(p + 0x00) = pawn;
            callFnP(g_savedCtrl[i], fn, p, 8, "Controller.Possess(pawn)");
        }
        g_savedCtrl[i] = NULL;
        ctrl = (F.PawnController > 0 &&
                !IsBadReadPtr((BYTE *)pawn + F.PawnController, 4))
             ? *(void **)((BYTE *)pawn + F.PawnController) : NULL;
    }
    if (!ctrl) {
        logf_("  [ai-restore] p%d no controller to hand the pawn back to", i);
        g_detached[i] = FALSE; g_aiReleased[i] = FALSE;
        g_leashOrig[i] = FALSE; g_savedCtrl[i] = NULL;
        g_savedGS[i] = 0; g_savedAR[i] = 0; g_pinned[i] = FALSE;
        return;
    }
    objName(ctrl, b, sizeof(b));
    if (!isCompanionCtrl(b)) {
        logf_("  [ai-restore] p%d controller '%s' is not a companion AI - "
              "leaving it alone", i, b);
        g_detached[i] = FALSE; g_aiReleased[i] = FALSE;
        g_leashOrig[i] = FALSE; g_savedCtrl[i] = NULL;
        g_savedGS[i] = 0; g_savedAR[i] = 0; g_pinned[i] = FALSE;
        return;
    }

    // Give the AI back animation control of the pawn.
    if (g_offCtrlAnims > 0)
        setBoolProp(ctrl, g_offCtrlAnims, g_maskCtrlAnims, TRUE);

    // Re-link the follow chain captured at take-over: this companion trails
    // LeadChar (Harry), inside its original follow radius, moving relative to
    // the lead. The lead's TrailingChar back-pointer must point at us too.
    void *lead = g_saveLead[i];
    if (!lead) lead = g_pawn[0] ? g_pawn[0] : findActorByClass("harry");
    if (lead) {
        if (!g_pawn[0]) g_pawn[0] = lead;
        if (g_offLeadChar > 0 && !IsBadWritePtr((BYTE *)ctrl + g_offLeadChar, 4)) {
            void **lc = (void **)((BYTE *)ctrl + g_offLeadChar);
            if (*lc != lead) {
                *lc = lead;
                logf_("  [ai-restore] p%d LeadChar -> %s", i,
                      objName(lead, b, sizeof(b)));
            }
        }
        if (g_offTrailingChar > 0 &&
            !IsBadWritePtr((BYTE *)lead + g_offTrailingChar, 4)) {
            void **tc = (void **)((BYTE *)lead + g_offTrailingChar);
            if (*tc != pawn) {
                *tc = pawn;
                logf_("  [ai-restore] p%d re-linked from lead.TrailingChar", i);
            }
        }
    }
    if (g_offFollowRadius > 0 &&
        !IsBadWritePtr((BYTE *)ctrl + g_offFollowRadius, 4)) {
        float *r = (float *)((BYTE *)ctrl + g_offFollowRadius);
        float want = (g_leashOrig[i] && g_saveRadius[i] >= 0.0f &&
                      g_saveRadius[i] < 40000.0f)
                   ? g_saveRadius[i]
                   : (*r > 40000.0f ? 300.0f : *r);   // generous default
        if (*r != want) {
            *r = want;
            logf_("  [ai-restore] p%d TrailCharFollowRadius %.0f -> %.0f",
                  i, *r, want);
        }
    }
    if (g_offUseDistLead > 0)
        setBoolProp(ctrl, g_offUseDistLead, g_maskUseDistLead,
                    g_leashOrig[i] ? g_saveUseDist[i] : TRUE);

    // Un-freeze the pawn so the AI's Tick can drive it again: restore the
    // walk stats the idle driver pinned to 0 and make sure physics is Walking.
    if (F.GroundSpeed > 0 && g_savedGS[i] > 0.0f &&
        !IsBadWritePtr((BYTE *)pawn + F.GroundSpeed, 4))
        *(float *)((BYTE *)pawn + F.GroundSpeed) = g_savedGS[i];
    if (F.AccelRate > 0 && g_savedAR[i] > 0.0f &&
        !IsBadWritePtr((BYTE *)pawn + F.AccelRate, 4))
        *(float *)((BYTE *)pawn + F.AccelRate) = g_savedAR[i];
    if (F.Physics > 0 && !IsBadWritePtr((BYTE *)pawn + F.Physics, 1) &&
        *((BYTE *)pawn + F.Physics) == 0)
        *((BYTE *)pawn + F.Physics) = 1;   // PHYS_Walking

    logf_("  [ai-restore] p%d handed to AI: ctrl=%p phys=%d "
          "(Pawn.Controller now=%p)", i, ctrl,
          F.Physics > 0 ? *((BYTE *)pawn + F.Physics) : -1,
          (F.PawnController > 0 && !IsBadReadPtr((BYTE *)pawn + F.PawnController, 4))
              ? *(void **)((BYTE *)pawn + F.PawnController) : NULL);

    // We no longer own this pawn; a future split re-take starts fresh.
    g_detached[i]   = FALSE;
    g_aiReleased[i] = FALSE;
    g_leashOrig[i]  = FALSE;
    g_savedCtrl[i]  = NULL;
    g_savedGS[i]    = 0;
    g_savedAR[i]    = 0;
    g_pinned[i]     = FALSE;
    g_letAI[i]      = FALSE;
}

// When the split is switched OFF, hand every player-2..N pawn we took control
// of back to its companion AI. Called on the split-off edge (key or file).
static void handBackAllPlayers(void)
{
    // v54: cancel borrowed P1/companion cast state before their normal AI is
    // restored; otherwise a split-off during the over-10-second fallback could
    // leave a companion visually/spiritually holding the old target.
    coopStopHolderForSplitShutdown();
    coopClearAll("split-screen disabled");
    for (int i=1;i<8;i++) { destroyAimFX(i); g_aimViewValid[i]=FALSE; }
    if (!F.ok) return;
    int N = g_numPlayers; if (N < 1) N = 1; if (N > 8) N = 8;
    for (int i = 1; i < N && i < 8; i++) {
        void *pawn = g_pawn[i];
        if (!pawn) continue;
        if (!g_detached[i] && !g_aiReleased[i] && !g_savedCtrl[i]) continue;
        aiRestore(i, pawn);
    }
}

// Everything cached that belongs to the current level. Called on a detected
// level change, and by the "n" debug key so the recovery path can be tested
// deterministically instead of hoping to catch a door transition.
static void invalidateLevelCaches(const char *reason)
{
    logf_("*** %s: dropping cached actors and re-resolving", reason);
    // Must run while old pawn pointers are still available. On an actual map
    // transition they may already be gone; coopRestore then safely cancels the
    // bridge without dereferencing them.
    coopClearAll("level cache invalidation");
    g_coopCursorClass = g_coopFnCursorLock = g_coopFnCursorUnlock = NULL;
    for (int k = 0; k < 8; k++) {
        destroyAimFX(k); g_aimViewValid[k]=FALSE;
        g_pawn[k] = NULL; g_pawnName[k][0] = 0;
        g_detached[k] = FALSE; g_aiReleased[k] = FALSE; g_letAI[k] = FALSE;
        g_savedCtrl[k] = NULL; g_camDistCur[k] = 0.0f;
        g_leashOrig[k] = FALSE; g_saveRadius[k] = 0;
        g_saveLead[k] = NULL; g_saveUseDist[k] = TRUE;
        g_savedGS[k] = 0; g_savedAR[k] = 0; g_pinned[k] = FALSE;
        g_aimFX[k] = NULL; g_aimFXAt[k] = 0;   // dies with the level anyway
        memset(&g_run[k], 0, sizeof(g_run[k]));
        g_castActor[k] = NULL;
        g_beginCastAt[k] = g_castTickAt[k] = g_animEndAt[k] = 0;
        g_suppressLandUntil[k] = g_hopkillT0[k] = 0;
        g_lastAirAt[k] = g_lastMoveAt[k] = 0;
        g_holdWalkUntil[k] = 0; g_holdArmed[k] = g_holdResync[k] = FALSE;
    }
    nativeAimResetBindings();
    g_nAimFXFr=0; memset(g_texAimFXFr,0,sizeof(g_texAimFXFr));
    g_fnResolved = FALSE;      // UFunctions live in packages too
    g_defaultSpell = NULL;
    cgResetLevel();            // v51: class index / candidates / pending hits
    g_origCursor = NULL;   // v51: it dies with the level
    resetViewYaw();
    F.ok = FALSE;              // re-resolve field offsets as well
}

// Input for split view i (i >= 1).
struct PadState { float fwd, side, turn, lookx, looky; BOOL jump, cast, use; };

// XInput (Xbox 360 / DualSenseX / most wrappers). Delay-loaded so a machine
// without the DLL still runs. DualSense native is DirectInput, not XInput -
// that is why DualSenseX "Xbox 360" did nothing in v10: we never called it.
#ifndef XINPUT_GAMEPAD_A
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_X              0x4000
#define XINPUT_GAMEPAD_Y              0x8000
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#endif
typedef struct { WORD wButtons; BYTE bLeftTrigger; BYTE bRightTrigger;
                 SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XI_PAD;
typedef struct { DWORD dwPacketNumber; XI_PAD Gamepad; } XI_STATE;
typedef DWORD (WINAPI *PFN_XIGetState)(DWORD, XI_STATE *);
typedef void (WINAPI *PFN_XIEnable)(BOOL);
static PFN_XIGetState g_XIGetState = NULL;
static PFN_XIEnable   g_XIEnable   = NULL;
static BOOL g_xiTried = FALSE;

static void loadXInput(void)
{
    if (g_xiTried) return;
    g_xiTried = TRUE;
    const char *dlls[] = { "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll", NULL };
    for (int d = 0; dlls[d]; d++) {
        HMODULE m = LoadLibraryA(dlls[d]);
        if (!m) {
            logf_("[pad] XInput LoadLibrary(%s) failed (err=%lu)",
                  dlls[d], (unsigned long)GetLastError());
            continue;
        }
        // v12: export probing by name AND ordinal. Wrappers (DualSenseX /
        // ViGEm) frequently export by ordinal only, and xinput9_1_0.dll on
        // real Windows exports XInputGetState ONLY as ordinal 2 - the name
        // never resolves there. v11 probed by name only.
        FARPROC p = NULL; const char *how = NULL;
        if ((p = GetProcAddress(m, "XInputGetState")) != NULL)
            how = "name XInputGetState";
        else if ((p = GetProcAddress(m, MAKEINTRESOURCEA(2))) != NULL)
            how = "ordinal 2";
        else if ((p = GetProcAddress(m, MAKEINTRESOURCEA(100))) != NULL)
            how = "ordinal 100 (GetStateEx)";
        if (p) {
            g_XIGetState = (PFN_XIGetState)p;
            logf_("[pad] XInput %s -> XInputGetState via %s (%p)",
                  dlls[d], how, (void *)p);
            g_XIEnable = (PFN_XIEnable)GetProcAddress(m, "XInputEnable");
            if (g_XIEnable) {
                g_XIEnable(TRUE);
                logf_("[pad] XInputEnable(TRUE) called");
            }
            return;
        }
        logf_("[pad] XInput %s loaded but no GetState export (name/2/100)",
              dlls[d]);
    }
    logf_("[pad] XInput not available - DirectInput only");
}

static float axisNorm(DWORD pos)
{
    return ((float)pos - 32767.0f) / 32767.0f;
}
static float deadz(float v, float dz)
{
    if (v > -dz && v < dz) return 0.0f;
    float s = (v > 0.0f) ? 1.0f : -1.0f;
    return s * (fabsf(v) - dz) / (1.0f - dz);
}
static float xiStick(SHORT v, SHORT dz)
{
    float f = (float)v, d = (float)dz;
    if (f > -d && f < d) return 0.0f;
    float s = (f > 0.0f) ? 1.0f : -1.0f;
    return s * (fabsf(f) - d) / (32767.0f - d);
}
// Analog trigger whose rest is an extreme (0 or 65535), DualSense L2/R2.
static float trigFromRest(DWORD pos, DWORD rest)
{
    if (rest < 12000)
        return (float)pos / 65535.0f;
    if (rest > 53000)
        return (65535.0f - (float)pos) / 65535.0f;
    return 0.0f;
}
static BOOL nameHas(const char *n, const char *frag)
{
    if (!n || !frag) return FALSE;
    size_t N = strlen(n), F = strlen(frag);
    if (F == 0 || F > N) return FALSE;
    for (size_t i = 0; i + F <= N; i++) {
        size_t k = 0;
        for (; k < F; k++) {
            char a = n[i + k], b = frag[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (k == F) return TRUE;
    }
    return FALSE;
}
static BOOL isSonyName(const char *n)
{
    return nameHas(n, "dual") || nameHas(n, "wireless controller") ||
           nameHas(n, "playstation") || nameHas(n, "sony") ||
           nameHas(n, "ps5") || nameHas(n, "ps4") || nameHas(n, "ds4");
}

enum { PAD_UNK = 0, PAD_XINPUT = 1, PAD_SONY = 2, PAD_XBOXDI = 3 };

// Player i (>=1) reads pad (i-1). XInput first (DualSenseX / Xbox), then
// winmm. DualSense HID: LS=X/Y, RS=Z/Rz, L2=Rx, R2=Ry, Cross=btn2, Square=btn1.
static void readPad(int i, PadState *out)
{
    if (i < 1 || i >= 8) return;
    UINT id = (UINT)(i - 1);
    loadXInput();

    if (g_XIGetState) {
        // v12: scan ALL slots 0-3 and claim the first connected pad for this
        // player (player 3 takes the next one). v11 polled only slot i-1=0;
        // ViGEm/DualSenseX virtual pads routinely land on another slot, which
        // is exactly why DualSenseX "did not respond at all".
        static int  xiSlot[8];
        static BOOL xiInit = FALSE;
        if (!xiInit) {
            for (int k = 0; k < 8; k++) xiSlot[k] = -1;
            xiInit = TRUE;
        }
        if (xiSlot[i] < 0) {
            DWORD codes[4] = { 0, 0, 0, 0 };
            int found = -1;
            for (DWORD s = 0; s < 4; s++) {
                XI_STATE st;
                memset(&st, 0, sizeof(st));
                codes[s] = g_XIGetState(s, &st);
                if (codes[s] != 0) continue;        // not connected
                BOOL taken = FALSE;
                for (int j = 0; j < 8; j++)
                    if (j != i && xiSlot[j] == (int)s) { taken = TRUE; break; }
                if (!taken) { found = (int)s; break; }
            }
            if (found >= 0) {
                xiSlot[i] = found;
                logf_("[pad] player %d CLAIMED XInput slot %d - A=jump RT=cast "
                      "right stick=camera (DualSenseX / Xbox)", i, found);
            } else {
                static DWORD xiNoneAt[8] = {0};
                DWORD nowc = GetTickCount();
                if (!xiNoneAt[i] || (nowc - xiNoneAt[i]) >= 30000) {
                    xiNoneAt[i] = nowc;
                    logf_("[pad] player %d: no free XInput pad on slots 0-3 "
                          "(codes %u %u %u %u) - trying DirectInput",
                          i, codes[0], codes[1], codes[2], codes[3]);
                }
            }
        }
        if (xiSlot[i] >= 0) {
            XI_STATE st;
            memset(&st, 0, sizeof(st));
            DWORD xr = g_XIGetState((DWORD)xiSlot[i], &st);
            if (xr != 0) {
                logf_("[pad] player %d LOST XInput slot %d (%lu) - re-scanning",
                      i, xiSlot[i], (unsigned long)xr);
                xiSlot[i] = -1;
            } else {
                const float DZ_L = 7849.0f, DZ_R = 8689.0f;
                float ly = xiStick(st.Gamepad.sThumbLY, (SHORT)DZ_L);
                float lx = xiStick(st.Gamepad.sThumbLX, (SHORT)DZ_L);
                out->fwd += ly;                     // XInput Y up = forward
                if (g_strafeMode) out->side += lx;
                else              out->turn += lx;
                out->lookx += xiStick(st.Gamepad.sThumbRX, (SHORT)DZ_R);
                // XInput RY up-positive -> PadState looky is Windows Y-down.
                out->looky += -xiStick(st.Gamepad.sThumbRY, (SHORT)DZ_R);
                if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A) out->jump = TRUE;
                if (st.Gamepad.wButtons & XINPUT_GAMEPAD_X) out->use  = TRUE;
                if (st.Gamepad.bRightTrigger > 30)          out->cast = TRUE;
                static DWORD lastXi[8] = {0};
                DWORD now = GetTickCount();
                if ((now - lastXi[i]) >= 2000) {
                    lastXi[i] = now;
                    logf_("[pad] p%d XInput slot=%d LS=(%+.2f %+.2f) RS=(%+.2f %+.2f) "
                          "LT=%u RT=%u btns=0x%04X",
                          i, xiSlot[i], lx, ly,
                          xiStick(st.Gamepad.sThumbRX, (SHORT)DZ_R),
                          xiStick(st.Gamepad.sThumbRY, (SHORT)DZ_R),
                          (unsigned)st.Gamepad.bLeftTrigger,
                          (unsigned)st.Gamepad.bRightTrigger,
                      (unsigned)st.Gamepad.wButtons);
                }
                return;     // do not also read HID, or DualSenseX + native would double
            }
        }
    }

    static char  padName[8][MAXPNAMELEN];
    static int   padProf[8]  = {0};
    static DWORD restRaw[8][6];
    static int   restN[8]    = {0};
    static BOOL  restOK[8]   = {0};

    // ---- v26: legacy-joystick live-scan ----------------------------------
    // v12 fixed the "wrong device" problem for XInput (virtual pads steal
    // slot 0); the HID path still polled a hard-coded id = i-1. The v25
    // hardware report: the DualSense was polled at id 0 through a generic
    // "Microsoft PC-joystick driver" entry that reports permanently
    // neutral axes (32767) and no buttons - an idle/virtual stick sitting
    // at id 0 while the real pad lives at another id. Fix: log the whole
    // joystick id map once, then watch all ids and LOCK onto the first
    // one that shows live input (a button, or an axis that MOVED between
    // scans - resting triggers at 0 do not count as movement).
    static int  joyLock[8];
    static BOOL joyInit = FALSE;
    if (!joyInit) {
        for (int k = 0; k < 8; k++) joyLock[k] = -1;
        joyInit = TRUE;
    }
    if (joyLock[i] < 0) {
        static BOOL  sMapped[8] = { FALSE };
        static DWORD sScanAt[8] = { 0 };
        static DWORD sPrevAx[8][16][6];
        static DWORD sPrevBtn[8][16];
        DWORD nowJ = GetTickCount();
        if (!sMapped[i]) {
            sMapped[i] = TRUE;
            for (UINT d2 = 0; d2 < 16; d2++) {
                JOYCAPSA c2; memset(&c2, 0, sizeof(c2));
                if (joyGetDevCapsA(d2, &c2, sizeof(c2)) == JOYERR_NOERROR)
                    logf_("[pad] joy id=%u name='%s' mid=%u pid=%u "
                          "btns=%u axes=%u",
                          d2, c2.szPname, c2.wMid, c2.wPid,
                          c2.wNumButtons, c2.wNumAxes);
            }
        }
        if (nowJ - sScanAt[i] >= 300) {
            sScanAt[i] = nowJ;
            for (UINT d2 = 0; d2 < 16; d2++) {
                if (d2 == id) continue;            // default already polled
                BOOL taken = FALSE;
                for (int j2 = 0; j2 < 8; j2++)
                    if (j2 != i && joyLock[j2] == (int)d2)
                        { taken = TRUE; break; }
                if (taken) continue;
                JOYINFOEX j2i; memset(&j2i, 0, sizeof(j2i));
                j2i.dwSize  = sizeof(j2i);
                j2i.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ |
                              JOY_RETURNR | JOY_RETURNU | JOY_RETURNV |
                              JOY_RETURNBUTTONS;
                if (joyGetPosEx(d2, &j2i) != JOYERR_NOERROR) continue;
                DWORD ax[6] = { j2i.dwXpos, j2i.dwYpos, j2i.dwZpos,
                                j2i.dwRpos, j2i.dwUpos, j2i.dwVpos };
                BOOL live = (j2i.dwButtons != 0);
                for (int a = 0; a < 6 && !live; a++) {
                    DWORD dv = (ax[a] > sPrevAx[i][d2][a])
                             ? ax[a] - sPrevAx[i][d2][a]
                             : sPrevAx[i][d2][a] - ax[a];
                    if (dv > 600) live = TRUE;
                }
                memcpy(sPrevAx[i][d2], ax, sizeof(ax));
                sPrevBtn[i][d2] = j2i.dwButtons;
                if (live) {
                    joyLock[i] = (int)d2;
                    padProf[i] = 0;               // re-profile the new device
                    restOK[i]  = FALSE;
                    restN[i]   = 0;
                    logf_("[pad] p%d LIVE input on joy id=%u - locking "
                          "(default id %u was dead)", i, d2, id);
                    id = d2;
                    break;
                }
            }
        }
    } else if ((UINT)joyLock[i] != id) {
        id = (UINT)joyLock[i];
    }

    JOYINFOEX ji;
    memset(&ji, 0, sizeof(ji));
    ji.dwSize  = sizeof(ji);
    ji.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ | JOY_RETURNR |
                 JOY_RETURNU | JOY_RETURNV | JOY_RETURNBUTTONS | JOY_RETURNPOV;
    MMRESULT er = joyGetPosEx(id, &ji);
    if (er != JOYERR_NOERROR) {
        static BOOL loggedMiss[8] = {0};
        if (!loggedMiss[i]) {
            loggedMiss[i] = TRUE;
            logf_("[pad] player %d: joyGetPosEx(id=%u) -> %u (no pad)", i, id, er);
        }
        return;
    }

    // (padName/padProf/restRaw/restN/restOK are declared above, before the
    // v26 live-scan block that resets them when a live pad is locked onto.)

    if (!padProf[i]) {
        JOYCAPSA caps;
        memset(&caps, 0, sizeof(caps));
        padName[i][0] = 0;
        if (joyGetDevCapsA(id, &caps, sizeof(caps)) == JOYERR_NOERROR) {
            strncpy(padName[i], caps.szPname, MAXPNAMELEN - 1);
            padName[i][MAXPNAMELEN - 1] = 0;
            logf_("[pad] player %d id=%u name='%s' mid=%u pid=%u buttons=%u "
                  "axes=%u caps=0x%lX (hasZ=%d hasR=%d hasU=%d hasV=%d hasPov=%d)",
                  i, id, caps.szPname, caps.wMid, caps.wPid, caps.wNumButtons,
                  caps.wNumAxes, (unsigned long)caps.wCaps,
                  (caps.wCaps & JOYCAPS_HASZ) ? 1 : 0,
                  (caps.wCaps & JOYCAPS_HASR) ? 1 : 0,
                  (caps.wCaps & JOYCAPS_HASU) ? 1 : 0,
                  (caps.wCaps & JOYCAPS_HASV) ? 1 : 0,
                  (caps.wCaps & JOYCAPS_HASPOV) ? 1 : 0);
        } else {
            logf_("[pad] player %d id=%u present but joyGetDevCaps failed", i, id);
        }
        // v12: the Sony map must NOT depend on the device name. Windows often
        // reports a localized or HID-generic name ("HID-compliant game
        // controller"), which kept v10/v11 on the Xbox map. A DualSense always
        // exposes >= 13 buttons and >= 6 axes through DirectInput; no Xbox or
        // generic pad reports that combination.
        const char *why = "default";
        padProf[i] = PAD_XBOXDI;
        if (isSonyName(padName[i])) {
            padProf[i] = PAD_SONY;
            why = "name";
        } else if (caps.wNumButtons >= 13 && caps.wNumAxes >= 6) {
            padProf[i] = PAD_SONY;
            why = "caps: >=13 buttons & >=6 axes";
        }
        logf_("[pad] p%d initial profile=%s (%s)", i,
              padProf[i] == PAD_SONY ? "SONY/DualSense" : "XBOX-DirectInput",
              why);
    }

    // Rest-sample while idle so we can tell sticks (centre ~32767) from
    // DualSense L2/R2 (rest 0). Needed if the device name is generic.
    if (!restOK[i] && restN[i] < 12 && ji.dwButtons == 0) {
        float lx = axisNorm(ji.dwXpos), ly = axisNorm(ji.dwYpos);
        if (lx > -0.20f && lx < 0.20f && ly > -0.20f && ly < 0.20f) {
            restRaw[i][0] += ji.dwXpos; restRaw[i][1] += ji.dwYpos;
            restRaw[i][2] += ji.dwZpos; restRaw[i][3] += ji.dwRpos;
            restRaw[i][4] += ji.dwUpos; restRaw[i][5] += ji.dwVpos;
            restN[i]++;
            if (restN[i] >= 8) {
                for (int a = 0; a < 6; a++) restRaw[i][a] /= (DWORD)restN[i];
                restOK[i] = TRUE;
                DWORD z = restRaw[i][2], r = restRaw[i][3];
                DWORD u = restRaw[i][4], v = restRaw[i][5];
                BOOL zC = (z > 18000 && z < 47000);
                BOOL rC = (r > 18000 && r < 47000);
                BOOL uT = (u < 12000 || u > 53000);
                BOOL vT = (v < 12000 || v > 53000);
                int old = padProf[i];
                // v12: rest evidence may only UPGRADE XBOX-DI to SONY. v11 let
                // it downgrade a caps-detected Sony pad back to the Xbox map,
                // which is how the pad stayed broken with a "same as before"
                // report.
                if (zC && rC && (uT || vT)) padProf[i] = PAD_SONY;
                logf_("[pad] p%d rest X=%lu Y=%lu Z=%lu R=%lu U=%lu V=%lu -> profile=%s%s",
                      i, restRaw[i][0], restRaw[i][1], z, r, u, v,
                      padProf[i] == PAD_SONY ? "SONY/DualSense" : "XBOX-DirectInput",
                      old != padProf[i] ? " (revised)" : "");
            }
        }
    }

    const float DZ = 0.22f;
    float ax = deadz(axisNorm(ji.dwXpos), DZ);
    float ay = deadz(axisNorm(ji.dwYpos), DZ);
    float az = deadz(axisNorm(ji.dwZpos), DZ);
    float ar = deadz(axisNorm(ji.dwRpos), DZ);
    float au = deadz(axisNorm(ji.dwUpos), DZ);
    float av = deadz(axisNorm(ji.dwVpos), DZ);

    out->fwd -= ay;
    if (g_strafeMode) out->side += ax;
    else              out->turn += ax;

    float rsx = 0.0f, rsy = 0.0f, rt = 0.0f;
    BOOL sony = (padProf[i] == PAD_SONY);
    if (sony) {
        // PCGW DualSense: RS = Z + Rz, L2 = Rx (U), R2 = Ry (V).
        rsx = az;
        rsy = ar;
        // Analog R2 only after rest proves V is a trigger (rest ~0). An
        // unused axis sitting at 32767 would otherwise look "half pulled".
        if (restOK[i] && (restRaw[i][5] < 12000 || restRaw[i][5] > 53000))
            rt = trigFromRest(ji.dwVpos, restRaw[i][5]);
        if (ji.dwButtons & JOY_BUTTON8) rt = 1.0f; // digital R2
        // Cross = jump (Xbox A). Square (btn1) = use (Xbox X). Do NOT treat
        // L2/R2 digital or L1/R1 as cast - that was v10 firing on both triggers.
        if (ji.dwButtons & JOY_BUTTON2) out->jump = TRUE;   // Cross
        if (ji.dwButtons & JOY_BUTTON1) out->use  = TRUE;   // Square
        if (ji.dwButtons & JOY_BUTTON3) out->use  = TRUE;   // Circle extra
        if (rt > 0.35f) out->cast = TRUE;
    } else {
        // Xbox 360 DirectInput: RS often R+U, combined triggers on Z.
        if (ar != 0.0f || au != 0.0f) { rsx = ar; rsy = au; }
        else if (av != 0.0f)          { rsx = av; rsy = 0.0f; }
        if (ji.dwButtons & JOY_BUTTON1) out->jump = TRUE;   // A
        if (ji.dwButtons & JOY_BUTTON3) out->use  = TRUE;   // X
        static BOOL zNeutral[8] = {0};
        if (az > -0.25f && az < 0.25f) zNeutral[i] = TRUE;
        if (zNeutral[i] && az < -0.40f) { out->cast = TRUE; rt = -az; }
    }
    out->lookx += rsx;
    out->looky += rsy;

    static DWORD lastLive[8] = {0};
    DWORD now = GetTickCount();
    if ((now - lastLive[i]) >= 2000) {
        lastLive[i] = now;
        // v12: RAW axis/button values, not just the deadzoned floats, so a
        // mapping that is still wrong can be re-derived from the log alone.
        logf_("[pad] p%d %s raw[X=%lu Y=%lu Z=%lu R=%lu U=%lu V=%lu btns=0x%lX] "
              "norm[X=%+.2f Y=%+.2f Z=%+.2f R=%+.2f U=%+.2f V=%+.2f] "
              "look=(%+.2f %+.2f) rt=%.2f",
              i, sony ? "SONY" : "XBDI",
              ji.dwXpos, ji.dwYpos, ji.dwZpos, ji.dwRpos, ji.dwUpos, ji.dwVpos,
              (unsigned long)ji.dwButtons,
              ax, ay, az, ar, au, av, rsx, rsy, rt);
    }
}

static void readInput(int i, PadState *out)
{
    out->fwd = out->side = out->turn = out->lookx = out->looky = 0.0f;
    out->jump = out->cast = out->use = FALSE;
    if (i < 1 || i >= 8) return;

    const PlayerKeys &k = g_pk[i];
    if (keyDown(k.fwd)   || keyDown(k.fwd2))   out->fwd  += 1.0f;
    if (keyDown(k.back)  || keyDown(k.back2))  out->fwd  -= 1.0f;
    if (keyDown(k.left)  || keyDown(k.left2))  out->side -= 1.0f;
    if (keyDown(k.right) || keyDown(k.right2)) out->side += 1.0f;

    if (keyDown(k.turnL)) out->turn -= 1.0f;
    if (keyDown(k.turnR)) out->turn += 1.0f;

    // Numpad presses that arrived as arrow VKs (NumLock off) and were taken
    // away from the game by the window hook.
    if (i == 1) {
        extern volatile LONG *numArrowState(void);
        volatile LONG *na = numArrowState();
        if (na[0]) out->fwd  += 1.0f;
        if (na[1]) out->fwd  -= 1.0f;
        if (na[2]) out->side -= 1.0f;
        if (na[3]) out->side += 1.0f;
    }

    readPad(i, out);
}

// Drive a controlled pawn from its player's input, relative to that view's yaw.
// Per-player view yaw. This MUST NOT be derived from the pawn's rotation.
// It previously was: the camera took its yaw from the pawn, that yaw was used
// as the movement basis, and the pawn was then turned to face the movement
// direction. Any sideways input therefore rotated the basis a little further
// every frame -- the pawn spun on the spot and never travelled. Forward was
// the only input with no rotation in it, which is why forward alone worked.
volatile LONG g_p2Moving[8] = {0};
static int   g_lookYaw[8]  = {0};          // right-stick camera yaw offset
static int   g_lookPitch[8]= {0};          // right-stick camera pitch offset
static int   g_jumpYawLock[8] = {0};
static DWORD g_jumpLockUntil[8] = {0};
static DWORD g_jumpLaunchAt[8]  = {0};
static float g_jumpVz[8]        = {0};
static DWORD g_jumpLogUntil[8]  = {0};   // v12: per-frame jump telemetry
static int   g_jumpLogN[8]      = {0};
static int   g_savedDesRot[8][3];        // v12: restore after the jump
static int   g_savedRotRate[8][3];
static BYTE  g_lastPhys[8] = {0};
static BOOL  g_yawInit[8]  = {0};
static DWORD g_lastTick[8] = {0};

static void driveePawn(int i, void *pawn)
{
    if (!F.ok || !pawn || IsBadReadPtr(pawn, 0x500)) return;
    if (i < 0 || i >= 8) return;
    PadState in; readInput(i, &in);
    g_padJump[i] = in.jump;
    g_padCast[i] = in.cast;
    g_padUse[i]  = in.use;

    float *vel = (float *)((BYTE *)pawn + F.Velocity);
    int   *rot = (int *)  ((BYTE *)pawn + F.Rotation);
    float *acc = (F.Acceleration > 0)
               ? (float *)((BYTE *)pawn + F.Acceleration) : NULL;

    if (!g_yawInit[i]) { g_viewYaw[i] = rot[1]; g_yawInit[i] = TRUE; }

    DWORD now = GetTickCount();
    float dt  = g_lastTick[i] ? (now - g_lastTick[i]) / 1000.0f : 0.016f;
    g_lastTick[i] = now;
    if (dt > 0.2f) dt = 0.2f;

    // v12: per-frame standing-jump telemetry. rot[] here is read BEFORE any of
    // our pins this frame, i.e. as the engine tick left it. On hardware this
    // line decides the next move: rawRot yaw drifting away while the mesh yaws
    // = something rotates the actor (fix: pin harder / find who). rawRot pinned
    // while the mesh yaws anyway = animation root (fix: anim side).
    if (now < g_jumpLogUntil[i] && g_jumpLogN[i] < 140) {
        g_jumpLogN[i]++;
        BYTE phLog = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 255;
        float *pvLog = (float *)((BYTE *)pawn + F.Velocity);
        logf_("    [jump] p%d rawRot=(%d %d %d) phys=%u vel=(%.0f %.0f %.0f) "
              "viewYaw=%d lock=%d ph=%d",
              i, rot[0], rot[1], rot[2], (unsigned)phLog,
              pvLog[0], pvLog[1], pvLog[2],
              g_viewYaw[i], g_jumpYawLock[i], g_jumpPh[i]);
    }

    // v12: standing jump spends one engine tick in PHYS_Walking BEFORE going
    // to PHYS_Falling. Walking->Falling takes the same anim path as a moving
    // jump (which never yaws the mesh); None->Falling picked the standing
    // jump anim that yaws it. Launch happens on a later frame so the engine
    // really ticks once with Walking.
    if (g_jumpPh[i] == 1 && now >= g_jumpLaunchAt[i]) {
        g_jumpPh[i] = 2;
        float *pvj = (float *)((BYTE *)pawn + F.Velocity);
        pvj[0] = pvj[1] = 0.0f;
        pvj[2] = g_jumpVz[i];
        if (F.Physics > 0) *((BYTE *)pawn + F.Physics) = 2;   // PHYS_Falling
        logf_("    jump launch: Walking->Falling vz=%.0f", g_jumpVz[i]);
    }

    // Fresh gamepad/keyboard jump state must veto the handoff in this frame,
    // not one frame later. A successful repair gets real walking ticks before
    // idle can put the pawn in PHYS_None again.
    BOOL castWalking = castGroundStep(i, pawn, now);
    if (in.jump || keyDown(g_pk[i].jump) || keyDown(g_pk[i].jump2))
        g_suppressLandUntil[i] = 0;

    // v42: airborne bookkeeping (single source of truth for the exit vetoes)
    {   BYTE phA = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 0;
        if (phA == 2) g_lastAirAt[i] = now;
    }
    // v43 GROUNDHOLD: the v42 hw log showed the engine's companion code
    // re-setting PHYS_Projectile(6) EVERY TICK through the post-cast window
    // (47 rescues in one session). Reactive fixes win the tick but lose
    // frames: anims froze (persistent cast pose) and rotation glitched
    // (wrong yaw). Now: on first sighting arm a 2.5s hold (refreshed on
    // every re-sighting); while armed, Walking is restored here EVERY frame
    // AND in the ProcessEvent detour (multiple times per frame, so no
    // rendered frame keeps a projectile tick). Cluster start logs once
    // (with the pawn's Base - the trigger context); when the hold expires
    // clean, one ChangeAnimation re-syncs the anim system.
    {   BYTE phR = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 0;
        float vzR = (F.Velocity > 0 && !IsBadReadPtr(pawn, F.Velocity + 12))
                  ? ((float *)((BYTE *)pawn + F.Velocity))[2] : 0.0f;
        BOOL grounded = g_jumpPh[i] == 0 && vzR > -60.0f && vzR < 60.0f;
        if (F.Physics > 0 && grounded && (phR == 5 || phR == 6)) {
            if (!g_holdArmed[i]) {
                g_holdArmed[i]  = TRUE;
                g_holdResync[i] = FALSE;
                char bb[96] = "?";
                if (F.Base > 0 && !IsBadReadPtr(pawn, F.Base + 4)) {
                    void *bs = *(void **)((BYTE *)pawn + F.Base);
                    if (bs) objName(bs, bb, sizeof(bb));
                }
                logf_("  [physfix] p%d phys=%u cluster start - hold armed "
                      "2.5s (base=%s)", i, (unsigned)phR, bb);
            }
            g_holdWalkUntil[i] = now + 2500;
            *((BYTE *)pawn + F.Physics) = 1;
        } else if (g_holdArmed[i] && now > g_holdWalkUntil[i]) {
            g_holdArmed[i] = FALSE;
            if (!g_holdResync[i] && g_fnChangeAnim) {
                g_holdResync[i] = TRUE;
                callFn(pawn, g_fnChangeAnim,
                       "ChangeAnimation [physrealign]");
                logf_("  [physrealign] p%d anim re-synced after physics "
                      "hold", i);
            }
        }
    }

    // Tank default: left/right TURN, matching the original player's arrows.
    // v13 default (FreeCamera=0): right stick X also TURNS pawn + camera
    // together, exactly like the original player - the character always faces
    // where the camera points and aims there. FreeCamera=1 keeps the old
    // free-orbit behaviour for testing.
    float turn = in.turn;
    if (!g_strafeMode) turn += in.side;
    if (!g_freeLook)   turn += in.lookx;
    if (turn < -0.05f || turn > 0.05f) {
        if (turn >  1.0f) turn =  1.0f;
        if (turn < -1.0f) turn = -1.0f;
        g_viewYaw[i] += (int)(turn * g_turnSpeed * dt * (65536.0f / 360.0f));
        g_viewYaw[i] &= 0xFFFF;
    }

    // FreeCamera mode only: right stick orbits the camera without turning the
    // pawn. In the default mode lookx went into the turn above and lookYaw
    // stays 0, so the camera sits directly behind the pawn at all times.
    if (g_freeLook && (in.lookx < -0.05f || in.lookx > 0.05f)) {
        float lx = in.lookx; if (lx > 1) lx = 1; if (lx < -1) lx = -1;
        g_lookYaw[i] += (int)(lx * 180.0f * dt * (65536.0f / 360.0f));
        g_lookYaw[i] &= 0xFFFF;
    }
    if (in.looky < -0.05f || in.looky > 0.05f) {
        float ly = in.looky; if (ly > 1) ly = 1; if (ly < -1) ly = -1;
        // stick up (negative Y on Windows) -> look up
        // v14: range widened (was +-8000/+4000 = only 14 deg of up-look) so
        // vertical aim actually matters; see playerCamPitch for the totals.
        g_lookPitch[i] += (int)(-ly * 120.0f * dt * (65536.0f / 360.0f));
        if (g_lookPitch[i] < -12000) g_lookPitch[i] = -12000;
        if (g_lookPitch[i] >  10000) g_lookPitch[i] =  10000;
    }

    // Standing jump: keep actor yaw pinned for the whole airtime, not a timer.
    // The mesh turn is the jump ANIM (PlayJump) - that call is skipped standing.
    // v12: also pin DesiredRotation (any FaceRotation pull now converges on
    // our yaw instead of the AI's) and zero RotationRate so nothing advances
    // toward a stale desired rotation during the air.
    {
        BYTE phNowLock = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 1;
        if (g_jumpStanding[i]) {
            g_viewYaw[i] = g_jumpYawLock[i];
            rot[0] = 0; rot[2] = 0;
            if (F.DesiredRotation > 0) {
                int *dr = (int *)((BYTE *)pawn + F.DesiredRotation);
                dr[0] = 0; dr[1] = g_jumpYawLock[i]; dr[2] = 0;
            }
            if (F.RotationRate > 0) {
                int *rr = (int *)((BYTE *)pawn + F.RotationRate);
                rr[0] = 0; rr[1] = 0; rr[2] = 0;
            }
            if (phNowLock != 2) {
                if (!g_jumpLockUntil[i])
                    g_jumpLockUntil[i] = now + 250;
                else if (now >= g_jumpLockUntil[i]) {
                    g_jumpStanding[i] = FALSE;
                    g_jumpPh[i] = 0;
                    g_jumpLockUntil[i] = 0;
                    // restore what we pinned during the jump so the game's
                    // own rotation systems see their pre-jump values again
                    if (F.DesiredRotation > 0) {
                        int *dr = (int *)((BYTE *)pawn + F.DesiredRotation);
                        dr[0] = g_savedDesRot[i][0];
                        dr[1] = g_savedDesRot[i][1];
                        dr[2] = g_savedDesRot[i][2];
                    }
                    if (F.RotationRate > 0) {
                        int *rr = (int *)((BYTE *)pawn + F.RotationRate);
                        rr[0] = g_savedRotRate[i][0];
                        rr[1] = g_savedRotRate[i][1];
                        rr[2] = g_savedRotRate[i][2];
                    }
                }
            } else {
                g_jumpLockUntil[i] = 0;
            }
        }
    }

    double yaw = g_viewYaw[i] * (6.283185307179586 / 65536.0);
    float  fx = (float)cos(yaw), fy = (float)sin(yaw);

    float fwd  = in.fwd;
    float side = g_strafeMode ? in.side : 0.0f;
    float mag  = sqrtf(fwd * fwd + side * side);

    // Capture the pawn's real walk stats once, before we pin them to 0 on idle.
    if (g_savedGS[i] <= 0.0f && F.GroundSpeed > 0) {
        float gs = *(float *)((BYTE *)pawn + F.GroundSpeed);
        if (gs > 50.0f && gs < 2000.0f) g_savedGS[i] = gs;
    }
    if (g_savedAR[i] <= 0.0f && F.AccelRate > 0) {
        float ar = *(float *)((BYTE *)pawn + F.AccelRate);
        if (ar > 50.0f && ar < 20000.0f) g_savedAR[i] = ar;
    }
    float speed = (g_savedGS[i] > 50.0f) ? g_savedGS[i] : 300.0f;
    float accel = (g_savedAR[i] > 50.0f) ? g_savedAR[i] : 2048.0f;

    InterlockedExchange(&g_p2Moving[i], (mag >= 0.15f) ? 1 : 0);
    if (mag >= 0.15f) g_lastMoveAt[i] = now;
    // v44 STANDING YAW LOCK: the companion AI FaceRotates the actor toward
    // its own preferred yaw EVERY TICK whenever nothing pins it (v43 hw
    // log: ~6 deg off between pin windows = the "wrong yaw while
    // standing"; Wine: it even pulls to a fixed 15691). Throttled writes
    // lose frames, so while STANDING (not moving, not airborne) this holds
    // the full pin every frame: DesiredRotation = view yaw AND
    // RotationRate = 0, so the engine's own rotation logic has nothing to
    // advance - the same mechanism as the v12 jump pin. Saved RotationRate
    // is restored the moment the
    // pawn moves or goes airborne; transition lines are logged.
    {
        static BOOL sYawLockOn[8]  = { 0 };
        static int  sSavedRR2[8][3] = {{ 0 }};
        BYTE phS = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 0;
        BOOL wantLock =
            g_p2Moving[i] == 0 && phS != 2 &&
            F.DesiredRotation > 0 && F.RotationRate > 0 &&
            !IsBadReadPtr(pawn, F.RotationRate + 12) &&
            !IsBadWritePtr(pawn, F.RotationRate + 12);
        if (wantLock && !sYawLockOn[i]) {
            int *rr = (int *)((BYTE *)pawn + F.RotationRate);
            sSavedRR2[i][0] = rr[0];
            sSavedRR2[i][1] = rr[1];
            sSavedRR2[i][2] = rr[2];
            sYawLockOn[i] = TRUE;
            logf_("  [yawhold] p%d engaged (standing lock)",
                  i);
        }
        if (sYawLockOn[i]) {
            if (wantLock) {
                int *dr = (int *)((BYTE *)pawn + F.DesiredRotation);
                dr[0] = 0; dr[1] = g_viewYaw[i]; dr[2] = 0;
                int *rr = (int *)((BYTE *)pawn + F.RotationRate);
                rr[0] = 0; rr[1] = 0; rr[2] = 0;
            } else {
                sYawLockOn[i] = FALSE;
                if (F.RotationRate > 0 &&
                    !IsBadWritePtr(pawn, F.RotationRate + 12)) {
                    int *rr = (int *)((BYTE *)pawn + F.RotationRate);
                    rr[0] = sSavedRR2[i][0];
                    rr[1] = sSavedRR2[i][1];
                    rr[2] = sSavedRR2[i][2];
                }
                logf_("  [yawhold] p%d released (moving/airborne)", i);
            }
        }
    }
    rot[1] = g_viewYaw[i];   // always face the view, like the original player

    float *pl = (F.Location > 0) ? (float *)((BYTE *)pawn + F.Location) : NULL;

    if (mag < 0.15f) {
        vel[0] = vel[1] = 0.0f;
        if (acc) acc[0] = acc[1] = acc[2] = 0.0f;
        // AI Tick writes Acceleration before physics (v6 idle drift). Pin
        // GroundSpeed to 0 so walking integration has nothing to apply.
        // Snap XY only for SMALL shoves; a huge jump is a cutscene teleport
        // and must become the new pin (v8 first Wine run froze Hermione in
        // the void by fighting a 4500-unit hub warp).
        if (F.GroundSpeed > 0) *(float *)((BYTE *)pawn + F.GroundSpeed) = 0.0f;
        if (F.AccelRate > 0)   *(float *)((BYTE *)pawn + F.AccelRate)   = 0.0f;
        BYTE phNow = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : 1;
        // PHYS_None while standing still: AI accel is not integrated -> no shake.
        // Jump handler (after us this frame) will set PHYS_Falling.
        // v12: skip while a standing jump is mid-prep - it needs PHYS_Walking
        // to survive one whole engine tick before launch. v49: likewise leave
        // Walking on until the post-cast floor handoff has been validated.
        if (phNow == 1 && F.Physics > 0 && g_jumpPh[i] == 0 && !castWalking)
            *((BYTE *)pawn + F.Physics) = 0;
        if (g_lastPhys[i] == 2 && phNow != 2) {
            // landed from a jump: v12 calls ChangeAnimation only. PlayWaiting
            // on land was a yaw suspect - its idle anim can swing the mesh to
            // face where the ANIM wants, not where we pinned the actor.
            if (g_fnChangeAnim) callFn(pawn, g_fnChangeAnim, "ChangeAnimation");
            else if (g_fnPlayWaiting) callFn(pawn, g_fnPlayWaiting, "PlayWaiting");
            rot[1] = g_viewYaw[i];
        }
        g_lastPhys[i] = (F.Physics > 0) ? *((BYTE *)pawn + F.Physics) : phNow;
        if (pl && phNow != 2) {
            if (!g_pinned[i]) {
                g_pinXY[i][0] = pl[0]; g_pinXY[i][1] = pl[1];
                g_pinned[i] = TRUE;
            } else {
                float ddx = pl[0] - g_pinXY[i][0], ddy = pl[1] - g_pinXY[i][1];
                float d2 = ddx * ddx + ddy * ddy;
                // Only follow teleports. Do NOT snap small drifts - that shook.
                if (d2 > 80.0f * 80.0f) {
                    g_pinXY[i][0] = pl[0]; g_pinXY[i][1] = pl[1];
                    static DWORD lastSnap[8] = {0};
                    DWORD n = GetTickCount();
                    if ((n - lastSnap[i]) >= 2000) {
                        lastSnap[i] = n;
                        logf_("  [ai-drift] p%d pin followed teleport %.0f units",
                              i, sqrtf(d2));
                    }
                }
            }
        } else {
            g_pinned[i] = FALSE;
        }
        if (g_wasMoving[i]) {
            g_wasMoving[i] = FALSE;
            if (g_fnPlayWaiting) callFn(pawn, g_fnPlayWaiting, "PlayWaiting");
            else if (g_fnChangeAnim) callFn(pawn, g_fnChangeAnim, "ChangeAnimation");
        }
        return;
    }
    g_pinned[i] = FALSE;
    if (F.GroundSpeed > 0) *(float *)((BYTE *)pawn + F.GroundSpeed) = speed;
    if (F.AccelRate > 0)   *(float *)((BYTE *)pawn + F.AccelRate)   = accel;
    if (F.Physics > 0) {
        BYTE ph = *((BYTE *)pawn + F.Physics);
        if (ph == 0) *((BYTE *)pawn + F.Physics) = 1;   // PHYS_Walking
    }
    if (mag > 1.0f) { fwd /= mag; side /= mag; }

    float dx = fx * fwd - fy * side;
    float dy = fy * fwd + fx * side;

    vel[0] = dx * speed; vel[1] = dy * speed;
    if (acc) { acc[0] = dx * accel; acc[1] = dy * accel; acc[2] = 0.0f; }
    if (!g_wasMoving[i]) {
        g_wasMoving[i] = TRUE;
        if (g_fnChangeAnim) callFn(pawn, g_fnChangeAnim, "ChangeAnimation");
    }
}

int  playerViewYaw(int i) { return (i >= 0 && i < 8) ? g_viewYaw[i] : 0; }
int  playerCamYaw(int i) {
    if (i < 0 || i >= 8) return 0;
    return (g_viewYaw[i] + g_lookYaw[i]) & 0xFFFF;
}
int playerCamPitch(int i) {
    int p = g_camPitch + ((i >= 0 && i < 8) ? g_lookPitch[i] : 0);
    // v14: with base -1400 this is about -73 deg down .. +47 deg up - close
    // to the original player's camera range. The old +8000 cap made
    // looking up (and aiming spells up) nearly impossible.
    if (p < -16000) p = -16000;
    if (p >  13000) p =  13000;
    return p;
}
void resetViewYaw(void)   {
    for (int k = 0; k < 8; k++) {
        g_yawInit[k] = FALSE;
        g_lookYaw[k] = 0; g_lookPitch[k] = 0;
        g_jumpLockUntil[k] = 0; g_lastPhys[k] = 0; g_jumpStanding[k] = FALSE;
        g_jumpPh[k] = 0; g_jumpLaunchAt[k] = 0; g_jumpVz[k] = 0;
        g_jumpLogUntil[k] = 0; g_jumpLogN[k] = 0;
        for (int a = 0; a < 3; a++) {
            g_savedDesRot[k][a] = 0; g_savedRotRate[k][a] = 0;
        }
    }
}

// ------------------------------- FFrame ------------------------------------


// ------------------------- captured camera state ---------------------------
static void  *g_viewport   = NULL;
static int    g_sizeX = 0, g_sizeY = 0;

static float  g_camLoc[3]  = {0,0,0};
static int    g_camRot[3]  = {0,0,0};
static float  g_camFOV     = 90.0f;
static volatile LONG g_haveCam = 0;

// ---------------------------- Draw hook ------------------------------------
typedef void (__fastcall *PFN_Draw)(void*, void*, void*, int, BYTE*, int*);
static PFN_Draw g_origDraw = NULL;
static Hook     g_hkDraw;
static LONG     g_frame = 0;

static void __fastcall Draw_detour(void *self, void *edx, void *Viewport,
                                   int Blit, BYTE *HitData, int *HitSize)
{
    LONG fn = InterlockedIncrement(&g_frame);
    {   // rolling FPS, so the cost of N extra scene renders is measurable
        static DWORD t0 = 0; static LONG f0 = 0;
        DWORD now = GetTickCount();
        if (!t0) { t0 = now; f0 = fn; }
        else if (now - t0 >= 5000) {
            logf_("[perf] %.2f fps over %lums (split=%s N=%d)",
                  (fn - f0) * 1000.0 / (now - t0), now - t0,
                  g_splitOn ? "ON" : "off", g_numPlayers);
            t0 = now; f0 = fn;
        }
    }
    if (Viewport && !IsBadReadPtr(Viewport, 0x100)) {
        g_viewport = Viewport;
        g_sizeX = *(int *)((BYTE *)Viewport + 0x8C);
        g_sizeY = *(int *)((BYTE *)Viewport + 0x90);
    }
    g_origDraw(self, edx, Viewport, Blit, HitData, HitSize);
}

// ------------------- FPlayerSceneNode ctor hook (camera) -------------------
// Captures the exact camera the engine computed for player 1 this frame.
typedef void *(__fastcall *PFN_PSN)(void*, void*, void*, void*, void*,
                                    float, float, float, int, int, int, float);
static PFN_PSN g_origPSN = NULL;
static Hook    g_hkPSN;

static void *__fastcall PSN_detour(void *self, void *edx, void *vp, void *rt,
                                   void *actor, float lx, float ly, float lz,
                                   int rp, int ry, int rr, float fov)
{
    g_camActor = actor;
    g_camLoc[0] = lx; g_camLoc[1] = ly; g_camLoc[2] = lz;
    g_camRot[0] = rp; g_camRot[1] = ry; g_camRot[2] = rr;
    g_camFOV = fov;
    if (InterlockedExchange(&g_haveCam, 1) == 0)
        logf_("camera captured: actor=%p loc=(%.1f %.1f %.1f) rot=(%d %d %d) fov=%.1f",
              actor, lx, ly, lz, rp, ry, rr, fov);
    return g_origPSN(self, edx, vp, rt, actor, lx, ly, lz, rp, ry, rr, fov);
}

// -------------------------- PostRender hook --------------------------------
typedef void (__fastcall *PFN_PostRender)(void*, void*, void*);
static PFN_PostRender g_origPostRender = NULL;
static Hook           g_hkPostRender;
static LONG           g_prCount   = 0;

static volatile LONG  g_portalOff = 0;   // set to 1 if a portal call misbehaves
static volatile LONG  g_liveFrames = 0;
static BOOL           g_dumped     = FALSE;

// Emit one DrawPortal call's parameter bytecode and invoke the native.
static void drawPortal(void *canvas, int x, int y, int w, int h,
                       void *camActor, const float loc[3], const int rot[3],
                       int fov, BOOL clearZ)
{
    BYTE bc[96];
    int  p = 0;
    #define PUT8(v)  bc[p++] = (BYTE)(v)
    #define PUT32(v) *(DWORD *)(bc + p) = (DWORD)(v); p += 4
    #define PUTF(v)  *(float *)(bc + p) = (float)(v); p += 4

    PUT8(g_ops[OP_INT].op); PUT32(x);
    PUT8(g_ops[OP_INT].op); PUT32(y);
    PUT8(g_ops[OP_INT].op); PUT32(w);
    PUT8(g_ops[OP_INT].op); PUT32(h);
    PUT8(g_ops[OP_OBJ].op); PUT32((DWORD)camActor);
    PUT8(g_ops[OP_VEC].op); PUTF(loc[0]); PUTF(loc[1]); PUTF(loc[2]);
    PUT8(g_ops[OP_ROT].op); PUT32(rot[0]); PUT32(rot[1]); PUT32(rot[2]);
    PUT8(g_ops[OP_INT].op); PUT32(fov);
    PUT8(clearZ ? g_ops[OP_TRUE].op : g_ops[OP_FALSE].op);
    PUT8(g_ops[OP_END].op);
    PUT8(g_ops[OP_END].op);   // guard byte

    FFrameLite st;
    memset(&st, 0, sizeof(st));
    st.Object = canvas;
    st.Code   = bc;

    BYTE result[64];
    memset(result, 0, sizeof(result));

    g_execDrawPortal(canvas, NULL, &st, result);
}

// Per-strip FOV. DrawPortal builds the projection from Viewport SizeX/SizeY
// (the FULL window) and the FOV we pass, then blits into the portal rect.
// Using the full-window aspect on a half-width strip horizontally squeezes
// the image. We temporarily set SizeX/SizeY to the portal size, and recompute
// FOV so the vertical field matches the original full-screen view.
static int portalFov(int w, int h)
{
    float base = (g_camFOV > 1.0f) ? g_camFOV : 85.0f;
    if (g_fovMode == 0 || g_sizeX <= 0 || g_sizeY <= 0 || w <= 0 || h <= 0)
        return (int)(base * g_fovScale + 0.5f);
    const float D2R = 0.01745329252f, R2D = 57.29577951f;
    float hf0 = base * D2R;
    float vf  = 2.0f * atanf(tanf(hf0 * 0.5f) * ((float)g_sizeY / (float)g_sizeX));
    float hf1 = 2.0f * atanf(tanf(vf  * 0.5f) * ((float)w / (float)h));
    float deg = hf1 * R2D * g_fovScale;
    if (deg < 40.0f) deg = 40.0f;
    if (deg > 140.0f) deg = 140.0f;
    return (int)(deg + 0.5f);
}

static void drawPortalSized(void *canvas, int x, int y, int w, int h,
                            void *camActor, const float loc[3], const int rot[3],
                            BOOL clearZ)
{
    int fov = portalFov(w, h);
    int *sx = NULL, *sy = NULL, osx = 0, osy = 0;
    if (g_viewport && !IsBadWritePtr((BYTE *)g_viewport + 0x90, 4)) {
        sx = (int *)((BYTE *)g_viewport + 0x8C);
        sy = (int *)((BYTE *)g_viewport + 0x90);
        osx = *sx; osy = *sy;
        *sx = w; *sy = h;
    }
    static BOOL loggedFov = FALSE;
    if (!loggedFov) {
        loggedFov = TRUE;
        logf_("[fov] full=%.0f (%dx%d) strip=%dx%d -> portalFov=%d (mode=%d scale=%.2f)",
              g_camFOV, g_sizeX, g_sizeY, w, h, fov, g_fovMode, g_fovScale);
    }
    drawPortal(canvas, x, y, w, h, camActor, loc, rot, fov, clearZ);
    if (sx) { *sx = osx; *sy = osy; }
}


// v52 native particles use bHidden masking only (hardware verification pending).
// The following v26 rationale applies only to the LEGACY sprite path.
// v26: per-pane visibility for the aim glow. The ORIGINAL game cursor is a
// per-view element - player 1 never sees player 2's cursor. v25 tried to
// get that by toggling bHidden around each portal draw; Wine honored it,
// hardware did NOT (the user still saw the glow inside P2 from P1's half
// - consistent with v22, where our bHidden CLEAR never made the borrowed
// cursor visible on hw either: bHidden writes do not affect the hw D3D8
// render path for these actors). Location writes, though, are proven on
// hardware (the glow moves/hugs surfaces there). So the glow is now
// PARKED far below the world (0,0,-100000) whenever its owner's pane is
// not the one being drawn - including the engine's base render - and
// restored to the real aim point for exactly its owner's portal.
// bHidden is still toggled as a belt-and-suspenders measure.
static void setAimFXPaneVisible(int panePlayer)
{
    nativeAimPaneVisible(panePlayer);
    if (!g_panePark) return;   // v27 isolation knob: pane parking off =
                               // glow visible in every pane (v24 behavior)
    static int   sOffH  = -2;
    static DWORD sMaskH = 0;
    if (sOffH == -2) {
        sOffH  = propOffset("Engine.Actor.bHidden");
        sMaskH = boolBitMask("Engine.Actor.bHidden");
    }
    if (F.Location <= 0) return;
    for (int j = 1; j < 8; j++) {
        if (g_nativeAimOwned[j]) continue; // never park native particles between ticks
        void *fx = g_aimFX[j];
        // v57: parking writes Location/bHidden through a cached pointer -
        // never write into an emitter that died with its level.
        if (!fx || !cgLiveObject(fx) || IsBadWritePtr((BYTE *)fx + F.Location, 12)) continue;
        float *al = (float *)((BYTE *)fx + F.Location);
        if (j == panePlayer && !g_aimSup[j]) {
            // owner's pane: draw it exactly where the aim math wants it
            al[0] = g_aimLast[j][0];
            al[1] = g_aimLast[j][1];
            al[2] = g_aimLast[j][2];
        } else {
            // every other pane + the engine's base render: parked in the
            // void, frustum-culled, invisible from anywhere
            al[0] = 0.0f; al[1] = 0.0f; al[2] = -100000.0f;
        }
        if (sOffH > 0 && sMaskH) {
            DWORD *slot = (DWORD *)((BYTE *)fx + (sOffH & ~3));
            if (j == panePlayer) *slot &= ~sMaskH;
            else                 *slot |=  sMaskH;
        }
    }
}

static BOOL renderSplitPortals(void *, void *, void *canvas)
{
    LONG n = InterlockedIncrement(&g_prCount);

    if (n <= 2 && canvas && !IsBadReadPtr(canvas, 0x80)) {
        logf_("PostRender #%ld canvas=%p  Canvas.SizeX/Y(+0x64,+0x68) = %d x %d "
              " ClipX/Y(+0x40,+0x44) = %.0f x %.0f",
              n, canvas, *(int *)((BYTE *)canvas + 0x64),
              *(int *)((BYTE *)canvas + 0x68),
              *(float *)((BYTE *)canvas + 0x40),
              *(float *)((BYTE *)canvas + 0x44));
    }

    // ---- split toggle (sampled FIRST, before any early return) ------------
    // This used to sit after the "not ready" checks, so on any frame where the
    // camera or canvas was not ready the key was not sampled at all. It is
    // also debounced: a single physical press can span many frames, and any
    // double-toggle would read as "the split flashed on and went away".
    {
        static BOOL  prevKey = FALSE;
        static DWORD lastToggle = 0;
        BOOL down = (GetAsyncKeyState(g_toggleKey) & 0x8000) != 0;
        DWORD now = GetTickCount();
        if (down && !prevKey && (now - lastToggle) >= 300) {
            lastToggle = now;
            BOOL wasOn = g_splitOn;
            g_splitOn = !g_splitOn;
            g_liveFrames = 0;
            logf_("toggle key 0x%02X -> split %s", g_toggleKey,
                  g_splitOn ? "ON" : "OFF");
            if (wasOn && !g_splitOn)
                handBackAllPlayers();   // split OFF: return P2..N to the AI
        }
        prevKey = down;
    }

    // ---- optional file trigger, for headless testing only -----------------
    // OFF by default. When it was always-on AND authoritative it silently
    // undid the key toggle every 30 frames; it is now opt-in and edge-only.
    if (g_fileToggle && (n % 30) == 0) {
        static int prevFile = -1;
        int f = (GetFileAttributesA("split_on") != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
        if (prevFile < 0)        prevFile = f;
        else if (f != prevFile) {
            BOOL wasOn = g_splitOn;
            g_splitOn = f ? TRUE : FALSE;
            g_liveFrames = 0;
            logf_("split_on file %s -> split %s",
                  f ? "appeared" : "removed", f ? "ON" : "OFF");
            if (wasOn && !g_splitOn)
                handBackAllPlayers();   // split OFF: return P2..N to the AI
        }
        prevFile = f;
    }

    // Level changes free every actor we cached. Detect it cheaply from the
    // camera actor's package name and drop all caches, or we dereference
    // freed memory the moment the player walks through a door.
    // v57: this used to run only on every 45th PostRender - at 80 fps that
    // left more than half a second after travel in which every cached pawn,
    // cursor and FX pointer pointed into the destroyed level. The hardware
    // crash entering Hogwarts (GetPathName <- GetPathName <- GetFullName
    // <- FPlayerSceneNode::Render) happened inside that window. The check is
    // now every frame (before any early return) and validates the camera
    // actor's liveness first, so a stale camera is dropped instead of named.
    if (g_camActor) {
        if (cgLiveObject(g_camActor)) {
            static char lastPkg[96] = "";
            char b[256], pkg[96] = "";
            objName(g_camActor, b, sizeof(b));
            const char *sp = strchr(b, ' ');
            const char *dot = sp ? strchr(sp + 1, '.') : NULL;
            if (sp && dot) {
                int L = (int)(dot - (sp + 1));
                if (L > 0 && L < 95) { memcpy(pkg, sp + 1, L); pkg[L] = 0; }
            }
            if (pkg[0] && strcmp(pkg, lastPkg)) {
                if (lastPkg[0]) {
                    char why[256];
                    _snprintf(why, sizeof(why) - 1, "level change '%s' -> '%s'", lastPkg, pkg);
                    why[sizeof(why) - 1] = 0;
                    invalidateLevelCaches(why);
                }
                strcpy(lastPkg, pkg);
            }
        } else {
            g_camActor = NULL;   // camera died with the old level: never name it
        }
    }

    // If the split is meant to be on but nothing is drawn, say why. Silent
    // early-returns are exactly what made the field bug hard to pin down.
    const char *blocked = NULL;
    if (!g_opsOK)                                  blocked = "opcodes not resolved";
    else if (!g_execDrawPortal)                    blocked = "execDrawPortal missing";
    else if (g_portalOff)                          blocked = "portals disabled";
    else if (!g_haveCam)                           blocked = "no camera captured yet";
    else if (!canvas || IsBadReadPtr(canvas, 0x80)) blocked = "bad canvas";
    else if (g_sizeX <= 0 || g_sizeY <= 0)         blocked = "screen size unknown";
    if (blocked) {
        static DWORD lastWhy = 0;
        DWORD now = GetTickCount();
        if (g_splitOn && (now - lastWhy) >= 5000) {
            lastWhy = now;
            logf_("[split] ON but not drawing: %s", blocked);
        }
        return FALSE;
    }

        if ((n % 30) == 0) {
            // One-shot level introspection, triggered by a "dump_objects" file.
            if (!g_dumped &&
                GetFileAttributesA("dump_objects") != INVALID_FILE_ATTRIBUTES) {
                g_dumped = TRUE;
                BOOL late = FALSE;
                {
                    HANDLE h = CreateFileA("dump_objects", GENERIC_READ,
                                           FILE_SHARE_READ, NULL,
                                           OPEN_EXISTING, 0, NULL);
                    if (h != INVALID_HANDLE_VALUE) {
                        char mb[8] = { 0 }; DWORD rd = 0;
                        if (ReadFile(h, mb, 7, &rd, NULL) &&
                            rd >= 4 && !strncmp(mb, "late", 4)) late = TRUE;
                        CloseHandle(h);
                    }
                }
                if (late) {
                    // The full boot dump freezes the game for minutes and
                    // eats the scripted key presses. "late" arms the aim
                    // probe only: it dumps classes + instances + probe FX
                    // from inside the first cast-hold, when everything is
                    // live. Only the offset calibration runs now.
                    logf_("--- late diagnostics: aim probe armed ---");
                    calibratePropertyOffset();
                    g_aimDiag = TRUE;
                }
                else
                {
                char b[512];
                logf_("--- object dump ---");
                logf_("  Viewport      = %p", g_viewport);
                if (g_viewport && !IsBadReadPtr(g_viewport, 0x40))
                    logf_("  Viewport.Actor= %s",
                          objName(*(void **)((BYTE *)g_viewport + 0x34), b, sizeof(b)));
                logf_("  camera actor  = %s", objName(g_camActor, b, sizeof(b)));

                // Derive the current level's package prefix ("HP3_Adv1Express.")
                // from the camera actor's full name, so this works on any map.
                static char pref[128];
                pref[0] = 0;
                {
                    const char *sp = strchr(b, ' ');
                    const char *dot = sp ? strchr(sp + 1, '.') : NULL;
                    if (sp && dot) {
                        int L = (int)(dot - (sp + 1)) + 1;
                        if (L > 0 && L < 120) { memcpy(pref, sp + 1, L); pref[L] = 0; }
                    }
                }
                logf_("  level prefix  = '%s'", pref);
                logf_("  canvas        = %s", objName(canvas, b, sizeof(b)));

                const char *filt[2] = { pref[0] ? pref : "Controller", NULL };
                dumpObjects(filt);

                dumpHeroFunctions();
                logf_("--- property offset calibration ---");
                calibratePropertyOffset();
                static const char *probes[] = {
                    "Engine.Actor.Velocity", "Engine.Actor.Acceleration",
                    "Engine.Actor.Physics", "Engine.Actor.Owner",
                    "Engine.Actor.Level", "Engine.Actor.DrawType",
                    "Engine.Pawn.Controller", "Engine.Pawn.Health",
                    "Engine.Pawn.GroundSpeed", "Engine.Pawn.bIsWalking",
                    "Engine.Controller.Pawn", "Engine.Controller.PlayerReplicationInfo",
                    "Engine.PlayerController.Player",
                    "Engine.PlayerController.ViewTarget",
                    "Engine.PlayerController.myHUD",
                    "Engine.PlayerController.DesiredFOV",
                    NULL
                };
                for (int q = 0; probes[q]; q++) {
                    int off = propOffset(probes[q]);
                    if (off >= 0) logf_("    %-46s = +0x%X", probes[q], off);
                    else          logf_("    %-46s = <not found>", probes[q]);
                }
                dumpActionSigs();
                dumpCursorSigs();
                g_aimDiag = TRUE;      // aim glow will probe FX classes once
                logf_("--- object dump end ---");
                }
            }
        }
    // v54/v55's class proof is deliberately learned from the unmodified game.
    // While split is OFF both companion AIs are free to demonstrate the stock
    // cooperative route, so the strict all-three certification runs here.
    // (cgWatchP1 runs every live split frame too, certifying from P1's own
    // stock cursor lock, so a split-ON session no longer dead-ends before a
    // P2/P3 >10-second hold can ever enter the shared path.)
    if (!g_splitOn) {
        // These are normally resolved by the first P2/P3 portal. Resolve them
        // here too so a fresh install can certify the stock P1 interaction
        // before the user has ever enabled split-screen.
        resolveFields();
        resolveActions();
        cgWatchP1();
        return FALSE;
    }

    LONG live = InterlockedIncrement(&g_liveFrames);
    if (live == 1) logf_("*** split-screen active: camera loc=(%.1f %.1f %.1f) "
                         "rot=(%d %d %d) fov=%.1f  screen=%dx%d N=%d",
                         g_camLoc[0], g_camLoc[1], g_camLoc[2],
                         g_camRot[0], g_camRot[1], g_camRot[2],
                         g_camFOV, g_sizeX, g_sizeY, g_numPlayers);

    int W = g_sizeX, H = g_sizeY, N = g_numPlayers;
    if (N < 1) N = 1;

    hookGameWindow();
    finishPendingCasts();
    cgTick();                  // v51: deliver pending spell hits
    cgWatchP1();               // v51: log player 1's real target/spell choice
    coopMonitor();             // v54: cancellation even if holder input vanished

    {   // K (diagnostic, 2-player configs only - K is P3's back key at N=3):
        // A/B the aim glow's actor DrawScale live - 4.0, 0.3, back to
        // normal. Settles whether the visible sparkle sizes with DrawScale
        // (it does only ABOVE the engine's fixed-size floor; see the
        // sizing note in aimFXVisuals).
        static BOOL prevK = FALSE;
        static int  stageK = 0;
        BOOL k = g_numPlayers <= 2 && keyDown('K');
        if (k && !prevK) {
            g_dbgGlowScale = (stageK == 0) ? 4.0f :
                             (stageK == 1) ? 0.3f : -1.0f;
            logf_("  [glowAB] K -> stage %d, DrawScale=%.1f%s", stageK,
                  g_dbgGlowScale,
                  g_dbgGlowScale < 0 ? " (normal pulsing)" : "");
            stageK = (stageK + 1) % 3;
        }
        prevK = k;
    }

    {   // D (diagnostic, 2-player configs): dump every live cursor/emitter
        // actor with full renderer fields - the P1-marker hunt. USE ON
        // HARDWARE: hold player 1's own aim (mouse) so the game's real
        // marker is on screen, tap D, send hp3mod.log.
        static BOOL prevD = FALSE;
        BOOL d = g_numPlayers <= 2 && keyDown('D');
        if (d && !prevD) diagDumpAimHunt();
        prevD = d;
    }

    {   // E (diagnostic, v31): 8-second ProcessEvent capture on the player
        // pawns. Tap E, then cast with P1 (mouse - the game's own natural
        // cast, which never hops), then cast with P2 (Y - ours). The log
        // gets BOTH exit sequences; the diff tells us exactly which calls
        // the engine makes that we do not. Patch auto-removes at window
        // end. See the pelog block above aimFXVisuals.
        static BOOL prevO = FALSE;
        BOOL oo = keyDown('O');
        if (oo && !prevO) diagDumpCastProps();   // v33: stuck-pose diff
        prevO = oo;
        static BOOL prevE = FALSE;
        BOOL e = keyDown('E');
        if (e && !prevE) pelogStart();
        prevE = e;
        if (g_pelogOn && GetTickCount() >= g_pelogEnd)
            pelogStop();   // render-loop safety: never rely on the
                           // detour alone to unpatch itself
    }

    {   // C (diagnostic): call the game's OWN makeCursor() on P1's real
        // controller, then dump the live SpellCursor at once and again
        // 1.5s later - ground truth for the original aim glow's visuals
        // (Style/Texture/DrawScale of its CursorParticles) and placement.
        static BOOL prevC = FALSE;
        static DWORD dumpAt = 0; static int dumpsLeft = 0;
        BOOL c = keyDown('C');
        BOOL cPress = c && !prevC;
        if (cPress) {
            void *ctrl = NULL;
            if (g_viewport && !IsBadReadPtr(g_viewport, 0x38))
                ctrl = *(void **)((BYTE *)g_viewport + 0x34);
            void *fn = findObjectByPath("KWGame.KWHeroController.makeCursor");
            logf_("  [origaim] C: ctrl=%p makeCursor=%p", ctrl, fn);
            if (ctrl && fn) {
                if (F.ControllerPawn > 0 &&
                    !IsBadReadPtr((BYTE *)ctrl + F.ControllerPawn, 4)) {
                    void *cp = *(void **)((BYTE *)ctrl + F.ControllerPawn);
                    if (cp && F.Location > 0 &&
                        !IsBadReadPtr(cp, F.Location + 12)) {
                        float *cl = (float *)((BYTE *)cp + F.Location);
                        logf_("  [origaim] ctrl pawn loc=(%.0f %.0f %.0f)",
                              cl[0], cl[1], cl[2]);
                    }
                }
                callFn(ctrl, fn, "makeCursor()");
                dumpCursorInstances();
                dumpsLeft = 2; dumpAt = GetTickCount() + 1500;
                // and spawn ONE bare SpellCursorEmitter (NO styling at all)
                // 200 units in front of P2 - factory-default visuals, to
                // compare against the game's own cursor look.
                {
                    void *p2 = getPawn(1);
                    if (p2 && F.Location > 0 &&
                        !IsBadReadPtr(p2, F.Location + 12)) {
                        float *pl = (float *)((BYTE *)p2 + F.Location);
                        int cy2 = playerCamYaw(1), cp2 = playerCamPitch(1);
                        double ry2 = cy2 * (6.283185307179586 / 65536.0);
                        double rp2 = cp2 * (6.283185307179586 / 65536.0);
                        float cpv2 = (float)cos(rp2);
                        float dir[3] = { (float)cos(ry2) * cpv2,
                                         (float)sin(ry2) * cpv2,
                                         (float)sin(rp2) };
                        float at[3] = { pl[0] + dir[0] * 200.0f,
                                        pl[1] + dir[1] * 200.0f,
                                        pl[2] + dir[2] * 200.0f + 40.0f };
                        int zr[3] = { 0, 0, 0 };
                        static void *s_bareFX = NULL;
                        s_bareFX = spawnFX(p2, g_clsAimFX, p2, at, zr);
                        logf_("  [origaim] BARE SpellCursorEmitter spawned "
                              "at (%.0f %.0f %.0f) - factory visuals:", 
                              at[0], at[1], at[2]);
                        dumpActorVisuals(s_bareFX, "bare (unstyled)");
                    }
                }
            }
        }
        prevC = c;
        if (dumpsLeft > 0 && GetTickCount() >= dumpAt) {
            logf_("  [origaim] cursor state +%.1fs:", 1.5f * (3 - dumpsLeft));
            dumpCursorInstances();
            dumpsLeft--; dumpAt = GetTickCount() + 1500;
        }
        // once: the game's own cursor emitter ARCHETYPE - texture (the
        // ParticleEmitter.Texture at +0x334, NOT Actor.Texture), size range
        // and opacity the game itself configured for the spell cursor.
        {
            static BOOL done = FALSE;
            if (cPress && !done) {
                done = TRUE;
                if (g_objArray) {
                    char nb[160];
                    for (int i = 0; i < g_objArray->Num; i++) {
                        void *o = g_objArray->Data[i];
                        if (!o || IsBadReadPtr(o, 0x338)) continue;
                        objName(o, nb, sizeof(nb));
                        if (!strstr(nb, "SpellCursorEmitterNoHit")) continue;
                        void *tex = *(void **)((BYTE *)o + 0x334);
                        float *ss = (float *)((BYTE *)o + 0x2B8);
                        logf_("  [origaim] archetype %p: ParticleEmitter."
                              "Texture=%p '%s' StartSizeRange min=(%.0f %.0f "
                              "%.0f) max=(%.0f %.0f %.0f) Opacity=%.2f", o,
                              tex, (tex && !IsBadReadPtr(tex, 8))
                                   ? objName(tex, nb, sizeof(nb)) : "?",
                              ss[0], ss[1], ss[2], ss[3], ss[4], ss[5],
                              *(float *)((BYTE *)o + 0xAC));
                        break;
                    }
                }
            }
        }
    }

    for (int i = 0; i < N; i++) {
        int x  = (int)((LONGLONG)W * i / N);
        int x2 = (int)((LONGLONG)W * (i + 1) / N);
        int w  = x2 - x;

        float loc[3] = { g_camLoc[0], g_camLoc[1], g_camLoc[2] };
        int   rot[3] = { g_camRot[0], g_camRot[1], g_camRot[2] };

        if (i > 0) {
            void *pawn = getPawn(i);
            if (pawn && !IsBadReadPtr(pawn, 0x170)) {
                resolveFields();
                resolveActions();
                takeControl(i, pawn);
                driveePawn(i, pawn);
                thirdPersonCam(i, pawn, loc, rot);
                memcpy(g_aimViewLoc[i],loc,12); memcpy(g_aimViewRot[i],rot,12); g_aimViewValid[i]=TRUE;
                {   // edge-triggered actions: "." = jump, "/" = cast
                    static BOOL prevJ[8] = {0}, prevF[8] = {0};
                    BOOL j = keyDown(g_pk[i].jump) || keyDown(g_pk[i].jump2) || g_padJump[i];
                    BOOL f = keyDown(g_pk[i].cast) || keyDown(g_pk[i].cast2) || g_padCast[i];
                    if (j && !prevJ[i]) {
                        // v13: grounded jumps only. v12 honored a jump press
                        // in mid-air: every rapid re-press reset vz to full
                        // jump speed during the flight and the pawn climbed
                        // indefinitely. Now a press is only honored when the
                        // pawn is not Falling and not mid walk-prep.
                        BYTE phJ = (F.Physics > 0)
                                 ? *((BYTE *)pawn + F.Physics) : 1;
                        BOOL moving = g_p2Moving[i] != 0;
                        int *pr = (int *)((BYTE *)pawn + F.Rotation);
                        float *pv = (float *)((BYTE *)pawn + F.Velocity);
                        float jz = 420.0f;
                        if (F.JumpZ > 0) {
                            float v = *(float *)((BYTE *)pawn + F.JumpZ);
                            if (v > 50.0f && v < 2000.0f) jz = v;
                        }
                        if (phJ == 2 || g_jumpPh[i] == 1) {
                            logf_("    jump ignored (%s, phys=%u) - "
                                  "grounded jumps only",
                                  phJ == 2 ? "airborne" : "mid-prep",
                                  (unsigned)phJ);
                        } else if (moving) {
                            // Moving jump: DoJump_Player + PlayJump were always
                            // fine on this path (v6+). PHYS Falling + vz.
                            callFn(pawn, g_fnJump, "DoJump_Player");
                            if (g_fnPlayJump) callFn(pawn, g_fnPlayJump, "PlayJump");
                            pv[2] = jz;
                            if (F.Physics > 0)
                                *((BYTE *)pawn + F.Physics) = 2;  // PHYS_Falling
                            logf_("    jump impulse (moving): vz=%.0f", jz);
                        } else {
                            g_jumpStanding[i]  = TRUE;
                            g_jumpYawLock[i]   = playerViewYaw(i);
                            g_jumpLockUntil[i] = 0;
                            if (F.DesiredRotation > 0) {
                                int *dr = (int *)((BYTE *)pawn + F.DesiredRotation);
                                g_savedDesRot[i][0] = dr[0];
                                g_savedDesRot[i][1] = dr[1];
                                g_savedDesRot[i][2] = dr[2];
                            }
                            if (F.RotationRate > 0) {
                                int *rr = (int *)((BYTE *)pawn + F.RotationRate);
                                g_savedRotRate[i][0] = rr[0];
                                g_savedRotRate[i][1] = rr[1];
                                g_savedRotRate[i][2] = rr[2];
                            }
                            pr[0] = 0; pr[1] = g_jumpYawLock[i]; pr[2] = 0;
                            pv[0] = pv[1] = 0.0f;
                            if (F.DesiredRotation > 0) {
                                int *dr = (int *)((BYTE *)pawn + F.DesiredRotation);
                                dr[0] = 0; dr[1] = g_jumpYawLock[i]; dr[2] = 0;
                            }
                            // Wine test 1 showed the engine rotates the pawn
                            // ~85 deg on the very first Walking tick after a
                            // long idle (rawRot yaw 167 -> 15691, vel 0).
                            // Kill that rotation DURING the prep tick itself:
                            // rate 0 stops the physics rotation advance.
                            if (F.RotationRate > 0) {
                                int *rr = (int *)((BYTE *)pawn + F.RotationRate);
                                rr[0] = 0; rr[1] = 0; rr[2] = 0;
                            }
                            g_jumpVz[i]       = jz;
                            g_jumpPh[i]       = 1;              // walk-prep
                            g_jumpLaunchAt[i] = GetTickCount() + 15;
                            if (F.Physics > 0)
                                *((BYTE *)pawn + F.Physics) = 1;   // PHYS_Walking
                            g_jumpLogUntil[i] = GetTickCount() + 1400;
                            g_jumpLogN[i]     = 0;
                            logf_("    jump prep: PHYS_Walking 1 tick then "
                                  "Falling vz=%.0f yawlock=%d playJump=0 "
                                  "desRotPinned=%d rotRatePinned=%d",
                                  jz, g_jumpYawLock[i],
                                  F.DesiredRotation > 0 ? 1 : 0,
                                  F.RotationRate > 0 ? 1 : 0);
                        }
                    }
                    if (f && !prevF[i]) {
                        // Restore a borrowed trio before PressedFire itself
                        // can write the real player's normal cast fields.
                        coopYieldToNormalCast(i);
                        callFn(pawn, g_fnFire, "PressedFire");
                        beginCast(i, pawn);
                    }
                    if (!f && prevF[i]) {
                        fireCast(i, pawn);
                        if (g_fnCharRelFire) callFn(pawn, g_fnCharRelFire, "HPCharacter.ReleasedFire");
                        callFn(pawn, g_fnFireRel, "ReleasedFire");
                    }
                    // v15: while the cast button is held, keep the game's own
                    // aim-glow emitter at the point this camera is aiming at
                    updateAimFX(i, pawn, f, loc, rot);
                    // ";" = use/interact, "," = hand the pawn over from the AI
                    static BOOL prevU[8] = {0}, prevP[8] = {0};
                    BOOL u = keyDown(g_pk[i].use) || keyDown(g_pk[i].use2) || g_padUse[i];
                    BOOL o = keyDown(g_pk[i].release);
                    if (u && !prevU[i]) doUse(i, pawn);
                    if (o && !prevP[i]) releaseAI(i, pawn, FALSE);   // soft
                    BOOL h = keyDown(g_pk[i].hard);
                    static BOOL prevH[8] = {0};
                    if (h && !prevH[i]) {
                        g_letAI[i] = !g_letAI[i];
                        if (g_letAI[i]) {
                            g_detached[i] = FALSE;
                            destroyAimFX(i);
                            repossess(i, pawn);
                            logf_("  [debug] p%d AI repossessed (M) - split will not fight it", i);
                        } else {
                            logf_("  [debug] p%d AI suppressed again (M)", i);
                        }
                    }
                    prevH[i] = h;
                    BOOL inval = keyDown(g_pk[i].inval);
                    static BOOL prevI = FALSE;
                    if (inval && !prevI && i == 1)
                        invalidateLevelCaches("forced cache invalidation (debug key)");
                    prevI = inval;
                    prevU[i] = u; prevP[i] = o;
                    prevJ[i] = j; prevF[i] = f;
                }
            } else {
                // No pawn for this slot: fall back to a yaw-offset view so the
                // strip is still visibly distinct rather than a clone.
                rot[1] = g_camRot[1] + i * (65536 / N);
            }
        }

        // movement telemetry, so a headless run can prove the pawn responds
        // Time-based, not frame-based: at 60 fps a frame counter would write
        // several lines a second and bloat the log on real hardware.
        static DWORD lastTel[8] = {0};
        DWORD nowTel = GetTickCount();
        if (i > 0 && i < 8 && g_pawn[i] && F.ok &&
            (nowTel - lastTel[i]) >= 2000) {
            lastTel[i] = nowTel;
            float *pl = (float *)((BYTE *)g_pawn[i] + F.Location);
            PadState dbg; readInput(i, &dbg);
            float *pv = (float *)((BYTE *)g_pawn[i] + F.Velocity);
            void *ctrl = (F.PawnController > 0)
                       ? *(void **)((BYTE *)g_pawn[i] + F.PawnController) : NULL;
            float dLead = -1.0f;
            void *lead = g_pawn[0] ? g_pawn[0] : findActorByClass("harry");
            if (lead && F.Location > 0 && !IsBadReadPtr(lead, F.Location + 12)) {
                float *hl = (float *)((BYTE *)lead + F.Location);
                float dx = pl[0]-hl[0], dy = pl[1]-hl[1], dz = pl[2]-hl[2];
                dLead = sqrtf(dx*dx + dy*dy + dz*dz);
            }
            logf_("  [t+%3ld] p%d in(fwd=%+.1f side=%+.1f turn=%+.1f look=%+.2f,%+.2f j=%d c=%d) loc=(%.0f %.0f Z=%.0f) vz=%.0f phys=%d yaw=%d ctrl=%p dLead=%.0f",
                  live, i, dbg.fwd, dbg.side, dbg.turn, dbg.lookx, dbg.looky,
                  dbg.jump ? 1 : 0, dbg.cast ? 1 : 0,
                  pl[0], pl[1], pl[2], pv[2],
                  F.Physics > 0 ? *((BYTE *)g_pawn[i] + F.Physics) : -1, playerViewYaw(i),
                  ctrl, dLead);
        }

        if (live == 1)
            logf_("  portal[%d]: rect=(%d,0 %dx%d) char=%s loc=(%.0f %.0f %.0f) yaw=%d",
                  i, x, w, H, i == 0 ? "<engine cam>" : g_pawnName[i],
                  loc[0], loc[1], loc[2], rot[1]);

        if (i>0 && (!g_pawn[i] || g_letAI[i] || !cgLiveObject(g_pawn[i]) ||
                    !actorInCurrentLevel(g_pawn[i]))) destroyAimFX(i);
        // v25: P2+'s aim glow renders only in its owner's pane
        setAimFXPaneVisible(i);
        drawPortalSized(canvas, x, 0, w, H, g_camActor, loc, rot, TRUE);
    }
    // v25: hidden everywhere else - including the engine's own base render
    setAimFXPaneVisible(-1);

    if (live == 1)   logf_("  >>> %d portals drawn, survived <<<", N);
    {   // once every 5 s: proof the split is still up, and for how long
        static DWORD lastBeat = 0, firstBeat = 0;
        DWORD now = GetTickCount();
        if (!firstBeat) firstBeat = now;
        if ((now - lastBeat) >= 5000) {
            lastBeat = now;
            logf_("[split] still ON: %d views, %ld frames, %.1f s since toggle",
                  N, live, (now - firstBeat) / 1000.0);
        }
    }
    if (live == 200) logf_("  >>> still alive after 200 split frames <<<");
    return TRUE;
}

// The HUD used to be drawn full-screen BEFORE the portals, which meant the
// portals painted straight over it and the split had no HUD at all. Now the
// portals go down first and the engine's own PostRender runs afterwards, with
// the canvas clipped to player 1's strip so the HUD lands inside that view
// instead of being stretched across every strip.
static void __fastcall PostRender_detour(void *self, void *edx, void *canvas)
{
    BOOL split = renderSplitPortals(self, edx, canvas);

    if (!split || C.ClipX < 0 || !canvas || IsBadReadPtr(canvas, 0x80)) {
        g_origPostRender(self, edx, canvas);
        return;
    }

    float *orgX  = (float *)((BYTE *)canvas + C.OrgX);
    float *orgY  = (float *)((BYTE *)canvas + C.OrgY);
    float *clipX = (float *)((BYTE *)canvas + C.ClipX);
    float *clipY = (float *)((BYTE *)canvas + C.ClipY);
    float sOrgX = *orgX, sOrgY = *orgY, sClipX = *clipX, sClipY = *clipY;

    int N = g_numPlayers; if (N < 1) N = 1; if (N > 8) N = 8;
    float stripW = (float)g_sizeX / (float)N;

    *orgX  = 0.0f;                       // player 1 owns the leftmost strip
    *orgY  = 0.0f;
    *clipX = stripW;
    *clipY = (float)g_sizeY;

    static BOOL logged = FALSE;
    if (!logged) {
        logged = TRUE;
        logf_("  [hud] clipped to strip 0: org=(0,0) clip=(%.0f,%.0f) "
              "(was clip=(%.0f,%.0f))", *clipX, *clipY, sClipX, sClipY);
    }

    g_origPostRender(self, edx, canvas);

    *orgX = sOrgX; *orgY = sOrgY; *clipX = sClipX; *clipY = sClipY;
}

// -------------------------------- init -------------------------------------
static DWORD WINAPI initThread(LPVOID)
{
    for (int w = 0; w < 240; w++) { Sleep(250); if (GetModuleHandleA("Engine.dll")) break; }
    Sleep(750);

    g_core   = GetModuleHandleA("Core.dll");
    g_engine = GetModuleHandleA("Engine.dll");
    logf_("--- init (t=%lu ms) Core=%p Engine=%p ---",
          GetTickCount(), (void *)g_core, (void *)g_engine);
    if (!g_core || !g_engine) { logf_("FATAL: modules missing"); return 0; }

    logf_("[opcode discovery]");
    g_opsOK = discoverOpcodes();
    logf_("  opcodes %s", g_opsOK ? "ALL RESOLVED" : "INCOMPLETE - portals disabled");

    g_ProcessEvent = (PFN_ProcessEvent)resolveThunk(
        (BYTE *)GetProcAddress(g_core, "?ProcessEvent@UObject@@UAEXPAVUFunction@@PAX1@Z"));
    g_objArray    = (TArrayLite *)GetProcAddress(g_core, SYM_GOBJ);
    g_GetFullName = (PFN_GetFullName)resolveThunk((BYTE *)GetProcAddress(g_core, SYM_FULLNAME));
    g_GetName     = (PFN_GetName)    resolveThunk((BYTE *)GetProcAddress(g_core, SYM_GETNAME));
    logf_("GObjObjects=%p (Num=%d)  GetFullName=%p  GetName=%p",
          (void *)g_objArray,
          (g_objArray && !IsBadReadPtr(g_objArray, 12)) ? g_objArray->Num : -1,
          (void *)g_GetFullName, (void *)g_GetName);

    g_execFastTrace = (PFN_execFastTrace)resolveThunk((BYTE *)GetProcAddress(
        g_engine, "?execFastTrace@AActor@@QAEXAAUFFrame@@QAX@Z"));
    logf_("execFastTrace = %p", (void *)g_execFastTrace);
    g_execSpawn = (PFN_execActorFn)resolveThunk((BYTE *)GetProcAddress(
        g_engine, "?execSpawn@AActor@@QAEXAAUFFrame@@QAX@Z"));
    g_execDestroy = (PFN_execActorFn)resolveThunk((BYTE *)GetProcAddress(
        g_engine, "?execDestroy@AActor@@QAEXAAUFFrame@@QAX@Z"));
    logf_("execSpawn = %p  execDestroy = %p",
          (void *)g_execSpawn, (void *)g_execDestroy);
    g_execSetPhysics = (PFN_execActorFn)resolveThunk((BYTE *)GetProcAddress(
        g_engine, "?execSetPhysics@AActor@@QAEXAAUFFrame@@QAX@Z"));
    g_FindBase = (PFN_FindBase)resolveThunk((BYTE *)GetProcAddress(
        g_engine, "?FindBase@AActor@@QAEXXZ"));
    logf_("execSetPhysics = %p  FindBase = %p (v49 floor handoff)",
          (void *)g_execSetPhysics, (void *)g_FindBase);
    g_execDrawPortal = (PFN_execDrawPortal)
        resolveThunk((BYTE *)GetProcAddress(g_engine, SYM_DRAWPORT));
    logf_("execDrawPortal = %p", (void *)g_execDrawPortal);

    logf_("[hooks]");
    if (installHook(&g_hkDraw, resolveThunk((BYTE *)GetProcAddress(g_engine, SYM_DRAW)),
                    (void *)&Draw_detour, "UGameEngine::Draw"))
        g_origDraw = (PFN_Draw)g_hkDraw.tramp;

    if (installHook(&g_hkPSN, resolveThunk((BYTE *)GetProcAddress(g_engine, SYM_PSNODE)),
                    (void *)&PSN_detour, "FPlayerSceneNode::ctor"))
        g_origPSN = (PFN_PSN)g_hkPSN.tramp;

    if (installHook(&g_hkPostRender, resolveThunk((BYTE *)GetProcAddress(g_engine, SYM_POSTREND)),
                    (void *)&PostRender_detour, "MasterProcessPostRender"))
        g_origPostRender = (PFN_PostRender)g_hkPostRender.tramp;

    logf_("--- init done, numPlayers=%d ---", g_numPlayers);
    return 0;
}

extern "C" __declspec(dllexport) void *WINAPI Direct3DCreate8(UINT SDKVersion)
{
    logf_("Direct3DCreate8(SDKVersion=%u)", SDKVersion);
    if (!g_realCreate) return NULL;
    void *r = g_realCreate(SDKVersion);
    logf_("  -> IDirect3D8* = %p", r);
    return r;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        InitializeCriticalSection(&g_logCs); g_logCsInit = TRUE;
        // Read the config before opening the log, so logging itself is
        // configurable ([debug] Log=0 for a silent release install).
        char ini[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, ini);
        strcat(ini, "\\hp3mod.ini");
        if (GetPrivateProfileIntA("debug", "Log", 1, ini))
            g_log = fopen("hp3mod.log", "w");
        logf_("===========================================================");
        logf_("=== hp3mod %s", MOD_STAMP);
        logf_("=== if this does not say %s you are running an OLD d3d8.dll", MOD_BUILD);
        logf_("===========================================================");

        int n = GetPrivateProfileIntA("split", "Players", 0, ini);
        if (n >= 1 && n <= 8) { g_numPlayers = n; logf_("hp3mod.ini: Players=%d", n); }
        int cd = GetPrivateProfileIntA("camera", "Distance",  0, ini);
        int ch = GetPrivateProfileIntA("camera", "Height",   -1, ini);
        int cp = GetPrivateProfileIntA("camera", "Pitch",  99999, ini);
        if (cd >  0)     g_camDist   = (float)cd;
        if (ch >= 0)     g_camHeight = (float)ch;
        if (cp != 99999) g_camPitch  = cp;
        g_camCollide = GetPrivateProfileIntA("camera",  "Collision", 1, ini) != 0;
        g_freeLook   = GetPrivateProfileIntA("camera",  "FreeCamera", 0, ini) != 0;
        g_aimedCast  = GetPrivateProfileIntA("actions", "AimedCast", 0, ini) != 0;
        g_castGameplay = GetPrivateProfileIntA("actions", "CastGameplay", 1, ini) != 0;
        g_castAutoHit  = GetPrivateProfileIntA("actions", "CastAutoHit", 1, ini) != 0;
        g_coopCastFallback = GetPrivateProfileIntA("actions", "CoopCastFallback", 1, ini) != 0;
        {   // v54: a deliberate lower bound keeps this from becoming a
            // short-press spell-target shortcut. Values above a minute are
            // clipped to keep GetTickCount retry/log arithmetic practical.
            int hold = GetPrivateProfileIntA("actions", "CoopCastHoldMs",
                                              (int)hp3coop::DefaultHoldMs, ini);
            if (hold < (int)hp3coop::DefaultHoldMs) hold = (int)hp3coop::DefaultHoldMs;
            if (hold > 60000) hold = 60000;
            g_coopCastHoldMs = (DWORD)hold;
        }
        logf_("hp3mod.ini: CoopCastFallback=%d CoopCastHoldMs=%lu",
              g_coopCastFallback ? 1 : 0, (unsigned long)g_coopCastHoldMs);
        {   // v40: gap between the state's natural AnimEnd and our exit.
            // 90 (default) = the v39 timing that removed the pose snap. If
            // a cast leaves a weird stance on hardware, raise it (250/400)
            // to move
            // back toward the v38 timing - or lower it toward 40. The code
            // fallback MUST equal the shipped ini default. (v49: the floor
            // handoff is separate via NoDip - this knob moves WHEN the
            // cast state ends, not how the landing looks.)
            int ca = GetPrivateProfileIntA("actions", "CastExitAdj", 90, ini);
            if (ca < 40) ca = 40;
            if (ca > 600) ca = 600;
            g_castExitAdj = ca;
        }
        {   // v19: aim glow marker size in percent (particle size scale).
            int gs = GetPrivateProfileIntA("actions", "GlowSize", 0, ini);
            if (gs >= 20 && gs <= 200) g_glowSize = gs;
        }
        g_nativeAim = GetPrivateProfileIntA("actions", "NativeAim", 1, ini) != 0;
        g_glowStyle = GetPrivateProfileIntA("actions", "GlowStyle", 6, ini);
        {   // v23: Cleanup default full - the state exit is REQUIRED (v22
            // hardware: without it the cast pose hangs forever, even while
            // running). "light" (no state exit) is hang-bait, kept only as
            // a documented experiment.
            char cb[16] = { 0 };
            GetPrivateProfileStringA("actions", "Cleanup", "full", cb,
                                     sizeof(cb) - 1, ini);
            g_cleanupLight = (_stricmp(cb, "light") == 0);
        }
        {   // v25: hop bisect. full (default) = the v23 chain that the v23
            // hardware run PROVED exits the pose (hw also proved noanimend
            // HANGS - the manual AnimEnd is required). min / exitanim are
            // experiments that drop hop-suspect calls.
            char cc[16] = { 0 };
            GetPrivateProfileStringA("actions", "CleanupChain", "full",
                                     cc, sizeof(cc) - 1, ini);
            if      (_stricmp(cc, "full")      == 0) g_cleanupChain = 0;
            else if (_stricmp(cc, "noanimend") == 0) g_cleanupChain = 1;
            else if (_stricmp(cc, "min")       == 0) g_cleanupChain = 2;
            else if (_stricmp(cc, "exitanim")  == 0) g_cleanupChain = 3;
        }
        {   // v25: fallback delay - ms after the fire before the exit chain
            // runs, used when the cast animation's own AnimEnd is not
            // visible (Wine never emits one). When it IS visible the chain
            // runs at AnimEnd + CastExitAdj instead (v40). Range 400..2600.
            int cd = GetPrivateProfileIntA("actions", "CleanupDelay", 1200,
                                           ini);
            if (cd >= 400 && cd <= 2600) g_cleanupDelay = cd;
        }
        {   // v49: native floor contact handoff. NoDip=0 keeps the legacy
            // animation-only fallback; neither mode restores a saved height.
            g_noDip = GetPrivateProfileIntA("actions", "NoDip", 1, ini) != 0;
        }
        {   // v24/v26 isolation knobs for the aim glow.
            g_bodyClear = GetPrivateProfileIntA("actions", "BodyClear", 1,
                                                ini) != 0;
            g_panePark  = GetPrivateProfileIntA("actions", "PanePark",  1,
                                                ini) != 0;
        }
        // v18: default straight-shot. The v17 default auto-aim cone kept
        // snapping the spell to other players / companions near the caster
        // while the glow marked where the camera points - the two disagreed.
        g_toggleKey  = iniKey("split", "ToggleKey", VK_F10, ini);
        g_fileToggle = GetPrivateProfileIntA("debug", "FileToggle", 0, ini) != 0;
        g_strafeMode = GetPrivateProfileIntA("player2", "StrafeMode", 0, ini) != 0;
        g_turnSpeed  = (float)GetPrivateProfileIntA("player2", "TurnSpeed", 180, ini);
        if (g_turnSpeed < 20.0f)  g_turnSpeed = 20.0f;
        if (g_turnSpeed > 400.0f) g_turnSpeed = 400.0f;
        g_fovMode  = GetPrivateProfileIntA("camera", "FovMode", 1, ini);
        {
            int fs = GetPrivateProfileIntA("camera", "FovScale", 100, ini);
            g_fovScale = fs / 100.0f;
            if (g_fovScale < 0.50f) g_fovScale = 0.50f;
            if (g_fovScale > 2.00f) g_fovScale = 2.00f;
        }
        logf_("hp3mod.ini: StrafeMode=%d TurnSpeed=%.0f FovMode=%d FovScale=%.2f "
              "FreeCamera=%d",
              g_strafeMode, g_turnSpeed, g_fovMode, g_fovScale, g_freeLook);
        // StartOn=1 brings the split up as soon as the level is running, with
        // no key press at all - both a convenience and a clean way to tell a
        // broken toggle apart from a broken renderer.
        g_splitOn    = GetPrivateProfileIntA("split", "StartOn", 0, ini) != 0;
        logf_("hp3mod.ini: ToggleKey=0x%02X StartOn=%d FileToggle=%d",
              g_toggleKey, g_splitOn, g_fileToggle);
        initKeyDefaults();
        loadKeyMap(ini);
        logf_("hp3mod.ini: camera dist=%.0f height=%.0f pitch=%d collision=%d aimedcast=%d castgameplay=%d",
              g_camDist, g_camHeight, g_camPitch, g_camCollide, g_aimedCast,
              g_castGameplay);

        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH); strcat(sys, "\\d3d8.dll");
        g_realD3D8 = LoadLibraryA(sys);
        if (g_realD3D8)
            g_realCreate = (PFN_Direct3DCreate8)GetProcAddress(g_realD3D8, "Direct3DCreate8");
        logf_("real d3d8 %p, create %p", (void *)g_realD3D8, (void *)g_realCreate);

        CreateThread(NULL, 0, initThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        logf_("=== detach (frames=%ld postrender=%ld) ===", g_frame, g_prCount);
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
