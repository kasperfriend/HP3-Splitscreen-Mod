// Minimal windows.h + mmsystem.h stub — host-side -fsyntax-only checks ONLY.
#ifndef WINSTUB_WINDOWS_H
#define WINSTUB_WINDOWS_H
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// ---- calling convention / misc macros (must precede typedefs) ----
#define MAX_PATH 260
#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define APIENTRY
#define APIPRIVATE
#define PASCAL
#define __fastcall
#define __stdcall
#define WINAPIV __cdecl
#define CDECL __cdecl
#define _cdecl __cdecl
#define UNALIGNED
#define CONST const
#define IN
#define OUT
#define OPTIONAL
#define __declspec(x)

// ---- base types ----
typedef int                 BOOL;
typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned long       DWORD;
typedef int                 INT;
typedef unsigned int        UINT;
typedef long                LONG;
typedef unsigned long       ULONG;
typedef float               FLOAT;
typedef char                CHAR;
typedef wchar_t             WCHAR;
typedef short               SHORT;
typedef unsigned int        SIZE_T;
typedef long long           LONGLONG;
typedef unsigned long long  ULONGLONG;
typedef long                LONG_PTR;
typedef unsigned long       ULONG_PTR;
typedef unsigned long       DWORD_PTR;
typedef DWORD_PTR          *PDWORD_PTR;
typedef unsigned int        UINT_PTR;
typedef int                 INT_PTR;
typedef long                LRESULT;
typedef unsigned int        WPARAM;
typedef long                LPARAM;
typedef int                 LCTYPE;
typedef long                HRESULT;
typedef DWORD               REGSAM;
typedef void               *LPVOID;
typedef const void         *LPCVOID;
typedef void               *HANDLE;
typedef HANDLE              HMODULE;
typedef HANDLE              HINSTANCE;
typedef HANDLE              HWND;
typedef HANDLE              HHOOK;
typedef HANDLE              HKEY;
typedef HKEY               *PHKEY;
typedef HANDLE              SC_HANDLE;
typedef HANDLE              HRAWFILE;
typedef HANDLE              HGDIOBJ2;
typedef void               *HDC;
typedef void               *HCURSOR;
typedef void               *HICON;
typedef void               *HBRUSH;
typedef void               *HFONT;
typedef void               *HGDIOBJ;
typedef void               *HKL;
typedef void               *FARPROC;
typedef DWORD               MMRESULT;
typedef DWORD               COLORREF;
typedef char               *LPSTR;
typedef const char         *LPCSTR;
typedef union { struct { DWORD LowPart; LONG HighPart; }; long long QuadPart; } LARGE_INTEGER;
typedef long (__fastcall   *WNDPROC)(void*, unsigned, unsigned, long);
typedef BOOL (__fastcall   *WNDENUMPROC)(HWND, LPARAM);
typedef void (__fastcall   *LPTOP_LEVEL_EXCEPTION_FILTER)(int, void*, void*);
typedef void (__stdcall    *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

struct CRITICAL_SECTION { int dummy; };
struct RECT { long left, top, right, bottom; };
struct POINT { long x, y; };
struct MSG { unsigned message; WPARAM wParam; LPARAM lParam; };
struct SECURITY_ATTRIBUTES { int nLength; };
struct OVERLAPPED { int Internal; };

// ---- constants ----
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#define VK_F10 0x70
#define VK_UP 0x26
#define VK_DOWN 0x28
#define VK_LEFT 0x25
#define VK_RIGHT 0x27
#define VK_NUMPAD0 0x60
#define VK_NUMPAD2 0x62
#define VK_NUMPAD4 0x64
#define VK_NUMPAD6 0x66
#define VK_NUMPAD8 0x68
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define WM_KEYDOWN 0x100
#define WM_KEYUP 0x101
#define WM_SYSKEYDOWN 0x104
#define WM_SYSKEYUP 0x105
#define WM_CHAR 0x102
#define WM_SETCURSOR 0x20
#define WM_ACTIVATE 0x6
#define WM_SIZE 0x5
#define WM_CLOSE 0x10
#define WM_DESTROY 0x2
#define WM_PAINT 0xF
#define WM_ACTIVATEAPP 0x1C
#define WM_HOTKEY 0x312
#define WM_APP 0x8000
#define WM_COMMAND 0x111
#define WM_SYSCOMMAND 0x112
#define WM_GETMINMAXINFO 0x24
#define WM_ENTERSIZEMOVE 0x231
#define WM_EXITSIZEMOVE 0x232
#define WM_XBUTTONDOWN 0x20B
#define WM_XBUTTONUP 0x20C
#define WM_MOUSEWHEEL 0x20A
#define WM_INPUT 0x255
#define WM_DISPLAYCHANGE 0x7E
#define WM_SETTINGCHANGE 0x1A
#define WM_DEVICECHANGE 0x219
#define WM_NCACTIVATE 0x86
#define WM_KILLFOCUS 8
#define WM_SETFOCUS 7
#define WM_LBUTTONDOWN 0x201
#define WM_LBUTTONUP 0x202
#define WM_RBUTTONDOWN 0x204
#define WM_RBUTTONUP 0x205
#define WM_MBUTTONDOWN 0x207
#define WM_MBUTTONUP 0x208
#define WM_MOUSEMOVE 0x200
#define WM_TIMER 0x113
#define WM_USER 0x400
#define GW_OWNER 4
#define GWL_EXSTYLE (-20)
#define GWL_STYLE (-16)
#define GWL_WNDPROC (-4)
#define GWLP_WNDPROC (-4)
#define GWL_HWNDPARENT (-8)
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_READWRITE 0x04
#define PAGE_NOACCESS 0x01
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_SET_FILE_POINTER 0xFFFFFFFF
#define NO_ERROR 0
#define ERROR_SUCCESS 0
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MORE_DATA 234
#define CP_ACP 0
#define CP_UTF8 65001
#define PM_REMOVE 1
#define PM_NOREMOVE 0
#define PM_QS_INPUT 0x200000
#define PM_QS_POSTMESSAGE 0x100000
#define PM_QS_PAINT 0x2000000
#define PM_QS_SENDMESSAGE 0x400000
#define QS_KEY 1
#define QS_MOUSE 2
#define QS_POSTMESSAGE 8
#define QS_TIMER 0x10
#define MWMO_INPUTAVAILABLE 4
#define MWMO_WAITALL 1
#define INFINITE 0xFFFFFFFF
#define WAIT_TIMEOUT 0x102
#define WAIT_OBJECT_0 0
#define WAIT_FAILED 0xFFFFFFFF
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
#define DLL_PROCESS_DETACH 0
#define WH_GETMESSAGE 3
#define WH_KEYBOARD 2
#define WH_MOUSE 7
#define WH_KEYBOARD_LL 13
#define WH_MOUSE_LL 14
#define HC_ACTION 0
#define LLKHF_INJECTED 0x10
#define LLMHF_INJECTED 1
#define SM_SWAPBUTTON 23
#define SM_MOUSEPRESENT 19
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define XBUTTON1 1
#define XBUTTON2 2
#define MK_LBUTTON 1
#define MK_RBUTTON 2
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_NUMLOCK 0x90
#define VK_ESCAPE 0x1B
#define VK_RETURN 0x0D
#define VK_TAB 0x09
#define VK_BACK 0x08
#define VK_SPACE 0x20
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_SNAPSHOT 0x2C
#define VK_CAPITAL 0x14
#define VK_SCROLL 0x91
#define VK_PAUSE 0x13
#define KEYEVENTF_EXTENDEDKEY 1
#define KEYEVENTF_KEYUP 2
#define KEYEVENTF_SCANCODE 8
#define INPUT_KEYBOARD 1
#define INPUT_MOUSE 0
#define INPUT_HARDWARE 2
#define MOUSEEVENTF_MOVE 1
#define MOUSEEVENTF_LEFTDOWN 2
#define MOUSEEVENTF_LEFTUP 4
#define MOUSEEVENTF_RIGHTDOWN 8
#define MOUSEEVENTF_RIGHTUP 0x10
#define MOUSEEVENTF_MIDDLEDOWN 0x20
#define MOUSEEVENTF_MIDDLEUP 0x40
#define MOUSEEVENTF_ABSOLUTE 0x8000
#define MOUSEEVENTF_WHEEL 0x800
#define JOYERR_NOERROR 0
#define JOY_RETURNPOV 0x40
#define JOY_RETURNX 2
#define JOY_RETURNY 4
#define JOY_RETURNZ 8
#define JOY_RETURNR 0x40
#define JOY_RETURNU 0x20
#define JOY_RETURNV 0x80
#define JOY_RETURNBUTTONS 0xC0
#define JOY_RETURNALL 0xFF
#define JOY_RETURNRAWDATA 0x80
#define JOYCAPS_HASPOV 0x100
#define JOYCAPS_HASR 0x40
#define JOYCAPS_HASU 0x20
#define JOYCAPS_HASV 0x80
#define JOYCAPS_HASZ 0x8
#define MAXPNAMELEN 32
#define MB_OK 0
#define MB_ICONERROR 0x10
#define MB_ICONINFORMATION 0x40
#define MB_YESNO 4
#define IDYES 6
#define IDNO 7
#define MB_SYSTEMMODAL 0x1000
#define MB_TOPMOST 0x40000
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWNORMAL 1
#define SW_MINIMIZE 6
#define SW_RESTORE 9
#define SWP_NOZORDER 0x4
#define SWP_FRAMECHANGED 0x20
#define SWP_NOMOVE 2
#define SWP_NOSIZE 1
#define SMTO_NORMAL 0
#define SMTO_BLOCK 1
#define HTCLIENT 1
#define HTCAPTION 2
#define HTTRANSPARENT (-1)
#define MA_NOACTIVATE 3
#define MA_NOACTIVATEANDEAT 4
#define CS_HREDRAW 2
#define CS_VREDRAW 1
#define CS_OWNDC 0x20
#define CW_USEDEFAULT 0x80000000
#define WS_OVERLAPPEDWINDOW 0xCF0000
#define WS_POPUP 0x80000000
#define WS_VISIBLE 0x10000000
#define WS_EX_TOPMOST 8
#define WS_EX_TOOLWINDOW 0x80
#define WS_EX_LAYERED 0x80000
#define WS_EX_TRANSPARENT 0x20
#define WS_EX_NOACTIVATE 0x8000000
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1
#define BI_RGB 0
#define CBM_INIT 4
#define SRCCOPY 0xCC0020
#define BLACKNESS 0x42
#define WHITENESS 0xFF0062
#define OBJ_BITMAP 7
#define CCHDEVICENAME 32
#define CCHFORMNAME 32
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000
#define LANG_NEUTRAL 0
#define REG_SZ 1
#define REG_DWORD 4
#define REG_EXPAND_SZ 2
#define RRF_RT_REG_SZ 2
#define RRF_RT_REG_DWORD 4
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_SET_VALUE 2
#define KEY_QUERY_VALUE 1
#define TOKEN_ADJUST_PRIVILEGES 0x20
#define TOKEN_QUERY 8
#define SE_PRIVILEGE_ENABLED 2
#define PROCESS_ALL_ACCESS 0x1F0FFF
#define PROCESS_VM_READ 0x10
#define PROCESS_QUERY_INFORMATION 0x400
#define TH32CS_SNAPMODULE 8
#define TH32CS_SNAPMODULE32 0x10
#define TH32CS_SNAPPROCESS 2
#define MAX_MODULE_NAME32 255
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x4550
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10b
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#define IMAGE_SCN_MEM_DISCARDABLE 0x2000000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x80
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x40
#define IMAGE_SCN_CNT_CODE 0x20
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#define IMAGE_DIRECTORY_ENTRY_IAT 12
#define IMAGE_DIRECTORY_ENTRY_EXPORT 0
#define IMAGE_DIRECTORY_ENTRY_BASERELOC 5
#define IMAGE_FILE_MACHINE_I386 0x14c
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_SIZEOF_SHORT_NAME 8
#define IMAGE_ORDINAL_FLAG32 0x80000000

// ---- structs (opaque for syntax checking) ----
struct IMAGE_DOS_HEADER { WORD e_magic; };
struct IMAGE_FILE_HEADER { WORD Machine; };
struct IMAGE_OPTIONAL_HEADER32 { WORD Magic; DWORD AddressOfEntryPoint; DWORD BaseOfCode; DWORD BaseOfData; DWORD ImageBase; DWORD SizeOfImage; DWORD DataDirectory[16][2]; };
struct IMAGE_NT_HEADERS { DWORD Signature; IMAGE_FILE_HEADER FileHeader; IMAGE_OPTIONAL_HEADER32 OptionalHeader; };
struct IMAGE_SECTION_HEADER { BYTE Name[8]; DWORD VirtualAddress; DWORD SizeOfRawData; DWORD PointerToRawData; DWORD Characteristics; };
struct IMAGE_IMPORT_DESCRIPTOR { DWORD FirstThunk; };
struct IMAGE_THUNK_DATA { DWORD u1; };
struct IMAGE_EXPORT_DIRECTORY { DWORD NumberOfFunctions; DWORD NumberOfNames; DWORD AddressOfFunctions; DWORD AddressOfNames; DWORD AddressOfNameOrdinals; DWORD Base; };
struct IMAGE_BASE_RELOCATION { DWORD VirtualAddress; DWORD SizeOfBlock; };
struct IMAGE_RELOCATION { DWORD VirtualAddress; };
struct ICONDIR { WORD idCount; };
struct CURSORDIR { WORD wWidth; };
struct RGBQUAD { BYTE rgbBlue; };
struct BITMAPINFOHEADER { DWORD biSize; LONG biWidth; LONG biHeight; WORD biPlanes; WORD biBitCount; DWORD biCompression; DWORD biSizeImage; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; };
struct WNDCLASSA { WNDPROC lpfnWndProc; LPCSTR lpszClassName; UINT style; HANDLE hInstance; HCURSOR hCursor; HBRUSH hbrBackground; HICON hIcon; };
struct KBDLLHOOKSTRUCT { DWORD vkCode; DWORD scanCode; DWORD flags; };
struct MSLLHOOKSTRUCT { POINT pt; DWORD mouseData; DWORD flags; };
struct KBDLLHOOKSTRUCT_*_unused;
struct MOUSEINPUT { LONG dx; LONG dy; DWORD mouseData; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; };
struct KEYBDINPUT { WORD wVk; WORD wScan; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; };
struct HARDWAREINPUT { DWORD uMsg; };
struct INPUT { DWORD type; MOUSEINPUT mi; KEYBDINPUT ki; HARDWAREINPUT hi; };
struct WINDOWPLACEMENT { UINT length; };
struct GUITHREADINFO { DWORD cbSize; };
struct MODULEENTRY32 { DWORD dwSize; char szModule[MAX_PATH]; char szExePath[MAX_PATH]; DWORD modBaseAddr; };
struct PROCESSENTRY32 { DWORD dwSize; DWORD th32ProcessID; char szExeFile[MAX_PATH]; };
struct STARTUPINFOA { DWORD cb; };
struct PROCESS_INFORMATION { HANDLE hProcess; HANDLE hThread; DWORD dwProcessId; };
struct TOKEN_PRIVILEGES { DWORD PrivilegeCount; };
struct OSVERSIONINFOA { DWORD dwOSVersionInfoSize; DWORD dwMajorVersion; DWORD dwMinorVersion; DWORD dwBuildNumber; char szCSDVersion[128]; };
struct SYSTEMTIME { WORD wYear; WORD wMonth; WORD wDay; };
struct TIME_ZONE_INFORMATION { LONG Bias; };
struct SHELLEXECUTEINFOA { DWORD cbSize; };
struct JOYINFOEX { DWORD dwSize; DWORD dwFlags; DWORD dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos, dwButtons, dwButtonNumber, dwPOV; };
struct JOYCAPSA { WORD wMid; WORD wPid; char szPname[MAXPNAMELEN]; UINT wNumButtons; UINT wNumAxes; WORD wCaps; };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor; RECT rcWork; };
struct MINMAXINFO { POINT ptMaxSize; };
struct WINDOWPOS { HWND hwnd; };
struct NCCALCSIZE_PARAMS { RECT rgrc[3]; };
struct STYLESTRUCT { DWORD styleOld; };
struct ALTTABINFO { DWORD cbSize; };
struct LUID { DWORD LowPart; };
struct MAKEINTRESOURCEA_stub;

// ---- functions ----
inline DWORD GetTickCount(void) { return 0; }
inline DWORD timeGetTime(void) { return 0; }
inline SHORT GetAsyncKeyState(int) { return 0; }
inline SHORT GetKeyState(int) { return 0; }
inline BOOL GetKeyboardState(BYTE*) { return 0; }
inline UINT MapVirtualKeyA(UINT, UINT) { return 0; }
inline int GetKeyNameTextA(LONG, char*, int) { return 0; }
inline LONG InterlockedExchange(volatile LONG*, LONG) { return 0; }
inline LONG InterlockedIncrement(volatile LONG*) { return 0; }
inline LONG InterlockedDecrement(volatile LONG*) { return 0; }
inline LONG InterlockedCompareExchange(volatile LONG*, LONG, LONG) { return 0; }
inline LONG InterlockedExchangeAdd(volatile LONG*, LONG) { return 0; }
inline void EnterCriticalSection(CRITICAL_SECTION*) { }
inline void LeaveCriticalSection(CRITICAL_SECTION*) { }
inline void InitializeCriticalSection(CRITICAL_SECTION*) { }
inline void DeleteCriticalSection(CRITICAL_SECTION*) { }
inline void Sleep(DWORD) { }
inline BOOL SwitchToThread(void) { return 0; }
inline HANDLE GetCurrentProcess(void) { return 0; }
inline DWORD GetCurrentProcessId(void) { return 0; }
inline DWORD GetCurrentThreadId(void) { return 0; }
inline HMODULE GetModuleHandleA(const char*) { return 0; }
inline BOOL GetModuleHandleExA(DWORD, const char*, HMODULE*) { return 0; }
inline DWORD GetModuleFileNameA(HMODULE, char*, DWORD) { return 0; }
inline HMODULE LoadLibraryA(const char*) { return 0; }
inline BOOL FreeLibrary(HMODULE) { return 0; }
inline FARPROC GetProcAddress(HMODULE, const char*) { return 0; }
inline UINT GetSystemDirectoryA(char*, UINT) { return 0; }
inline UINT GetWindowsDirectoryA(char*, UINT) { return 0; }
inline UINT GetPrivateProfileIntA(const char*, const char*, INT, const char*) { return 0; }
inline DWORD GetPrivateProfileStringA(const char*, const char*, const char*, char*, DWORD, const char*) { return 0; }
inline BOOL WritePrivateProfileStringA(const char*, const char*, const char*, const char*) { return 0; }
inline BOOL IsBadReadPtr(const void*, UINT_PTR) { return 0; }
inline BOOL IsBadWritePtr(const void*, UINT_PTR) { return 0; }
inline BOOL IsBadCodePtr(FARPROC) { return 0; }
inline HANDLE CreateFileA(const char*, DWORD, DWORD, SECURITY_ATTRIBUTES*, DWORD, DWORD, HANDLE) { return 0; }
inline BOOL CloseHandle(HANDLE) { return 0; }
inline BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, OVERLAPPED*) { return 0; }
inline BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, OVERLAPPED*) { return 0; }
inline DWORD GetFileSize(HANDLE, DWORD*) { return 0; }
inline DWORD SetFilePointer(HANDLE, LONG, LONG*, DWORD) { return 0; }
inline DWORD GetLastError(void) { return 0; }
inline void SetLastError(DWORD) { }
inline void OutputDebugStringA(const char*) { }
inline HWND GetForegroundWindow(void) { return 0; }
inline BOOL SetForegroundWindow(HWND) { return 0; }
inline HWND GetActiveWindow(void) { return 0; }
inline HWND GetFocus(void) { return 0; }
inline HWND SetFocus(HWND) { return 0; }
inline DWORD GetWindowThreadProcessId(HWND, DWORD*) { return 0; }
inline BOOL EnumWindows(WNDENUMPROC, LPARAM) { return 0; }
inline BOOL EnumChildWindows(HWND, WNDENUMPROC, LPARAM) { return 0; }
inline BOOL IsWindow(HWND) { return 0; }
inline BOOL IsWindowVisible(HWND) { return 0; }
inline BOOL IsIconic(HWND) { return 0; }
inline BOOL IsZoomed(HWND) { return 0; }
inline HWND GetWindow(HWND, UINT) { return 0; }
inline HWND GetParent(HWND) { return 0; }
inline BOOL GetClientRect(HWND, RECT*) { return 0; }
inline BOOL GetWindowRect(HWND, RECT*) { return 0; }
inline BOOL ClientToScreen(HWND, POINT*) { return 0; }
inline BOOL ScreenToClient(HWND, POINT*) { return 0; }
inline BOOL GetCursorPos(POINT*) { return 0; }
inline BOOL SetCursorPos(int, int) { return 0; }
inline int ShowCursor(BOOL) { return 0; }
inline HCURSOR SetCursor(HCURSOR) { return 0; }
inline HCURSOR LoadCursorA(HINSTANCE, LPCSTR) { return 0; }
inline HCURSOR LoadIconA(HINSTANCE, LPCSTR) { return 0; }
inline HWND SetCapture(HWND) { return 0; }
inline BOOL ReleaseCapture(void) { return 0; }
inline HWND GetCapture(void) { return 0; }
inline LONG SetWindowLongA(HWND, int, LONG) { return 0; }
inline LONG GetWindowLongA(HWND, int) { return 0; }
inline LONG SetWindowLongPtrA(HWND, int, LONG_PTR) { return 0; }
inline LONG GetWindowLongPtrA(HWND, int) { return 0; }
inline LRESULT CallWindowProcA(WNDPROC, HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return 0; }
inline BOOL MoveWindow(HWND, int, int, int, int, BOOL) { return 0; }
inline BOOL ShowWindow(HWND, int) { return 0; }
inline BOOL UpdateWindow(HWND) { return 0; }
inline BOOL InvalidateRect(HWND, const RECT*, BOOL) { return 0; }
inline BOOL PostMessageA(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline LRESULT SendMessageTimeoutA(HWND, UINT, WPARAM, LPARAM, UINT, UINT, PDWORD_PTR) { return 0; }
inline BOOL PeekMessageA(MSG*, HWND, UINT, UINT, UINT) { return 0; }
inline LRESULT DispatchMessageA(const MSG*) { return 0; }
inline BOOL TranslateMessage(const MSG*) { return 0; }
inline void keybd_event(BYTE, BYTE, DWORD, ULONG_PTR) { }
inline void mouse_event(DWORD, DWORD, DWORD, DWORD, ULONG_PTR) { }
inline UINT SendInput(UINT, void*, int) { return 0; }
inline UINT RegisterWindowMessageA(const char*) { return 0; }
inline BOOL RegisterHotKey(HWND, int, UINT, UINT) { return 0; }
inline BOOL UnregisterHotKey(HWND, int) { return 0; }
inline UINT GetDoubleClickTime(void) { return 0; }
inline UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC) { return 0; }
inline BOOL KillTimer(HWND, UINT_PTR) { return 0; }
inline DWORD MsgWaitForMultipleObjectsEx(DWORD, const HANDLE*, DWORD, DWORD, DWORD) { return 0; }
inline HHOOK SetWindowsHookExA(int, void*, HINSTANCE, DWORD) { return 0; }
inline BOOL UnhookWindowsHookEx(HHOOK) { return 0; }
inline LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM) { return 0; }
inline int MessageBoxA(HWND, const char*, const char*, UINT) { return 0; }
inline BOOL AdjustWindowRect(RECT*, DWORD, BOOL) { return 0; }
inline unsigned short RegisterClassExA(const void*) { return 0; }
inline HWND CreateWindowExA(DWORD, const char*, const char*, DWORD, int, int, int, int, HWND, HANDLE, HINSTANCE, void*) { return 0; }
inline BOOL DestroyWindow(HWND) { return 0; }
inline LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL SetWindowTextA(HWND, const char*) { return 0; }
inline int GetWindowTextA(HWND, char*, int) { return 0; }
inline int GetClassNameA(HWND, char*, int) { return 0; }
inline BOOL SetLayeredWindowAttributes(HWND, DWORD, BYTE, DWORD) { return 0; }
inline BOOL AttachThreadInput(DWORD, DWORD, BOOL) { return 0; }
inline BOOL GetGUIThreadInfo(DWORD, void*) { return 0; }
inline BOOL GetWindowPlacement(HWND, void*) { return 0; }
inline BOOL SetWindowPlacement(HWND, const void*) { return 0; }
inline BOOL SystemParametersInfoA(UINT, UINT, void*, UINT) { return 0; }
inline HKL GetKeyboardLayout(DWORD) { return 0; }
inline int GetKeyboardType(int) { return 0; }
inline int ToUnicode(UINT, UINT, const BYTE*, wchar_t*, int, UINT) { return 0; }
inline int ToAscii(UINT, UINT, const BYTE*, WORD*, UINT) { return 0; }
inline MMRESULT joyGetPosEx(UINT, void*) { return 0; }
inline MMRESULT joyGetDevCapsA(UINT, void*, UINT) { return 0; }
inline MMRESULT joySetCapture(HWND, UINT, UINT, BOOL) { return 0; }
inline MMRESULT joyReleaseCapture(UINT) { return 0; }
inline MMRESULT timeSetEvent(UINT, UINT, void*, DWORD, UINT) { return 0; }
inline MMRESULT timeKillEvent(MMRESULT) { return 0; }
inline MMRESULT timeBeginPeriod(UINT) { return 0; }
inline MMRESULT timeEndPeriod(UINT) { return 0; }
inline UINT waveOutGetNumDevs(void) { return 0; }
inline UINT midiOutGetNumDevs(void) { return 0; }
inline BOOL PlaySoundA(const char*, HANDLE, DWORD) { return 0; }
inline HDC GetDC(HWND) { return 0; }
inline int ReleaseDC(HWND, HDC) { return 0; }
inline HDC CreateCompatibleDC(HDC) { return 0; }
inline BOOL DeleteDC(HDC) { return 0; }
inline void* CreateDIBSection(HDC, const void*, UINT, void**, HANDLE, DWORD) { return 0; }
inline BOOL DeleteObject(HGDIOBJ) { return 0; }
inline HGDIOBJ SelectObject(HDC, HGDIOBJ) { return 0; }
inline BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return 0; }
inline BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return 0; }
inline int GetSystemMetrics(int) { return 0; }
inline void* VirtualAlloc(void*, SIZE_T, DWORD, DWORD) { return 0; }
inline BOOL VirtualFree(void*, SIZE_T, DWORD) { return 0; }
inline BOOL VirtualProtect(void*, SIZE_T, DWORD, DWORD*) { return 0; }
inline BOOL FlushInstructionCache(HANDLE, const void*, SIZE_T) { return 0; }
inline HANDLE CreateThread(void*, SIZE_T, unsigned long (__stdcall*)(void*), void*, DWORD, DWORD*) { return 0; }
inline BOOL DisableThreadLibraryCalls(HMODULE) { return 0; }
inline HANDLE CreateEventA(SECURITY_ATTRIBUTES*, BOOL, BOOL, const char*) { return 0; }
inline BOOL SetEvent(HANDLE) { return 0; }
inline BOOL ResetEvent(HANDLE) { return 0; }
inline DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
inline HANDLE OpenMutexA(DWORD, BOOL, const char*) { return 0; }
inline HANDLE CreateMutexA(SECURITY_ATTRIBUTES*, BOOL, const char*) { return 0; }
inline BOOL ReleaseMutex(HANDLE) { return 0; }
inline HANDLE OpenProcess(DWORD, BOOL, DWORD) { return 0; }
inline BOOL ReadProcessMemory(HANDLE, const void*, void*, SIZE_T, SIZE_T*) { return 0; }
inline BOOL WriteProcessMemory(HANDLE, void*, const void*, SIZE_T, SIZE_T*) { return 0; }
inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return 0; }
inline BOOL Module32First(HANDLE, void*) { return 0; }
inline BOOL Module32Next(HANDLE, void*) { return 0; }
inline BOOL Process32First(HANDLE, void*) { return 0; }
inline BOOL Process32Next(HANDLE, void*) { return 0; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER*) { return 0; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER*) { return 0; }
inline BOOL GetVersionExA(void*) { return 0; }
inline DWORD GetLogicalDrives(void) { return 0; }
inline UINT GetDriveTypeA(const char*) { return 0; }
inline HANDLE FindFirstFileA(const char*, void*) { return 0; }
inline BOOL FindNextFileA(HANDLE, void*) { return 0; }
inline BOOL FindClose(HANDLE) { return 0; }
inline DWORD GetFileAttributesA(const char*) { return 0; }
inline BOOL ShellExecuteExA(void*) { return 0; }
inline DWORD GetCurrentDirectoryA(DWORD, char*) { return 0; }
inline BOOL SetCurrentDirectoryA(const char*) { return 0; }
inline DWORD GetTempPathA(DWORD, char*) { return 0; }
inline DWORD ExpandEnvironmentStringsA(const char*, char*, DWORD) { return 0; }
inline DWORD GetShortPathNameA(const char*, char*, DWORD) { return 0; }
inline DWORD GetLongPathNameA(const char*, char*, DWORD) { return 0; }
inline LONG RegOpenKeyExA(HKEY, const char*, DWORD, REGSAM, PHKEY) { return 0; }
inline LONG RegQueryValueExA(HKEY, const char*, DWORD*, DWORD*, BYTE*, DWORD*) { return 0; }
inline LONG RegCloseKey(HKEY) { return 0; }
inline LONG RegCreateKeyExA(HKEY, const char*, DWORD, char*, DWORD, REGSAM, SECURITY_ATTRIBUTES*, PHKEY, DWORD*) { return 0; }
inline LONG RegSetValueExA(HKEY, const char*, DWORD, DWORD, const BYTE*, DWORD) { return 0; }
inline int LoadStringA(HINSTANCE, UINT, char*, int) { return 0; }
inline UINT GetProfileIntA(const char*, const char*, INT) { return 0; }
inline BOOL OpenProcessToken(HANDLE, DWORD, void**) { return 0; }
inline BOOL LookupPrivilegeValueA(const char*, const char*, void*) { return 0; }
inline BOOL AdjustTokenPrivileges(HANDLE, BOOL, void*, DWORD, void*, DWORD*) { return 0; }
inline void GlobalMemoryStatus(void*) { }
inline BOOL GetComputerNameA(char*, DWORD*) { return 0; }
inline BOOL GetUserNameA(char*, DWORD*) { return 0; }
inline void GetSystemInfo(void*) { }
inline BOOL IsWow64Process(HANDLE, BOOL*) { return 0; }
inline BOOL SetProcessWorkingSetSize(HANDLE, SIZE_T, SIZE_T) { return 0; }
inline BOOL GetProcessAffinityMask(HANDLE, ULONG_PTR*, ULONG_PTR*) { return 0; }
inline DWORD TlsAlloc(void) { return 0; }
inline void* TlsGetValue(DWORD) { return 0; }
inline BOOL TlsSetValue(DWORD, void*) { return 0; }
inline BOOL TlsFree(DWORD) { return 0; }
inline BOOL IsDebuggerPresent(void) { return 0; }
inline LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f) { return f; }
inline BOOL SetDllDirectoryA(const char*) { return 0; }
inline int MultiByteToWideChar(UINT, DWORD, const char*, int, wchar_t*, int) { return 0; }
inline int WideCharToMultiByte(UINT, DWORD, const wchar_t*, int, char*, int, const char*, BOOL*) { return 0; }
inline int lstrlenA(const char*) { return 0; }
inline char* lstrcpyA(char*, const char*) { return 0; }
inline char* lstrcatA(char*, const char*) { return 0; }
inline char* CharUpperA(char*) { return 0; }
inline int _snprintf(char*, size_t, const char*, ...) { return 0; }
inline int _stricmp(const char*, const char*) { return 0; }
inline int _strnicmp(const char*, const char*, size_t) { return 0; }
inline BOOL AttachConsole_x(DWORD) { return 0; }
#define JOY_BUTTON1 1
#define JOY_BUTTON2 2
#define JOY_BUTTON3 4
#define JOY_BUTTON8 0x80
#define MAKEINTRESOURCEA(i) ((LPCSTR)((ULONG_PTR)(WORD)(i)))
#define INVALID_HANDLE_VALUE_2 0
#endif  // WINSTUB_WINDOWS_H
