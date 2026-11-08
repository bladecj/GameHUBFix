/*
  GameHUBFix.dll v2 - ONE plugin for all three GameHUB 2 / gamepad problems.

  CONCURRENCY (v2, deliberately simple)
    v1 used a timer-queue callback plus a CRITICAL_SECTION shared with
    Rainmeter's main thread. The callback held the lock while calling
    SetWindowPos on explorer's taskbar windows. Those calls are synchronous
    cross-process operations: when a game exits the shell is busy rebuilding
    the taskbar, the callback stalled while holding the lock, and every
    Update()/Finalize() on the main thread blocked forever -> Rainmeter hung
    and had to be killed from Task Manager.

    v2 has NO background thread, NO critical section and NO self LoadLibrary
    reference. All work is driven by Update(), which Rainmeter calls on its
    main thread (PadListener.ini Update=100). Every SetWindowPos uses
    SWP_ASYNCWINDOWPOS so the main thread can never block on explorer.
    With one thread there is nothing to deadlock on.

    Historical evidence that occluded skins keep updating: the previous
    GameGuard gate was Update()-driven on PadListener and worked while the
    skin was reported "covered" by a full-screen game.

  THREE JOBS
    A) START GATE (Update -> 1 = a game is running, block Start)
       Generic "launch session" detection, no game exe names at all.
    B) FOREGROUND / SKIN Z-ORDER (Launch / Settle bangs)
       ALT tap, force-activate, AllowSetForegroundWindow(ASFW_ANY), push
       desktop overlays to HWND_BOTTOM for SettleMs.
    C) TASKBAR Z-ORDER (automatic)
       Temporarily take the explorer taskbar strip out of the topmost group
       while a full-screen application is frontmost; restore its exact
       GWL_EXSTYLE and Z-order predecessor when the full-screen window goes.

  CONFIG (GameHUBFix.ini next to the DLL; never written by Rainmeter)
      [GameHUBFix]
      Enabled=1
      TickMs=250        ; minimum interval for layer work (gate is every Update)
      SettleMs=10000
      Coverage=0.98
      SessionMs=90000

  Exports: Initialize / Reload / Update / GetString / ExecuteBang / Finalize.
  Two skins load this same DLL; all state is process-global and main-thread only.
*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "RainmeterAPI.h"

#ifndef ASFW_ANY
#define ASFW_ANY ((DWORD)-1)
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
#ifndef SWP_ASYNCWINDOWPOS
#define SWP_ASYNCWINDOWPOS 0x4000
#endif

/* Asynchronous: never wait for explorer.exe to process a Z-order change. */
static const UINT SWP_ASYNC =
    SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
    SWP_NOSENDCHANGING | SWP_ASYNCWINDOWPOS;

static HINSTANCE  g_hInst = NULL;
static bool       g_enabled = true;
static DWORD      g_tickMs = 250;
static DWORD      g_settleMs = 10000;
static DWORD      g_sessionMs = 90000;
static double     g_coverage = 0.98;
static volatile LONG g_cycles = 0, g_launches = 0, g_pushes = 0;
static volatile LONG g_begins = 0, g_restores = 0;
static double     g_gameValue = 0.0;
static std::wstring g_diag;
static ULONGLONG  g_lastLayerTick = 0;
static ULONGLONG  g_settleDeadline = 0;
static bool       g_trayActive = false;
static int        g_instances = 0;

/* ------------------------------------------------------------------ utils */

static std::string Lower(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] + 32);
    return s;
}

static std::wstring W(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

struct PEntry { DWORD pid; std::string name; };

static std::vector<PEntry> Snapshot()
{
    std::vector<PEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe;
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            PEntry e;
            e.pid = pe.th32ProcessID;
            std::string n;
            for (int i = 0; pe.szExeFile[i] && i < MAX_PATH; ++i)
            {
                wchar_t c = pe.szExeFile[i];
                if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
                n.push_back(c < 128 ? (char)c : '?');
            }
            e.name = Lower(n);
            out.push_back(e);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

static bool PidAlive(const std::vector<PEntry>& s, DWORD pid)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i].pid == pid) return true;
    return false;
}

static const char* NameForPid(const std::vector<PEntry>& s, DWORD pid)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i].pid == pid) return s[i].name.c_str();
    return "";
}

/* System / storefront / browser helpers. NOT a game list: no game ever needs
   to be added here. It only keeps background helpers that happen to start
   together with a game out of the launch session. */
static const char* kPlatform[] = {
    "rainmeter.exe", "explorer.exe", "dwm.exe", "svchost.exe", "conhost.exe",
    "runtimebroker.exe", "searchhost.exe", "searchapp.exe",
    "startmenuexperiencehost.exe", "shellexperiencehost.exe",
    "applicationframehost.exe", "systemsettings.exe", "dllhost.exe",
    "audiodg.exe", "fontdrvhost.exe", "ctfmon.exe", "taskhostw.exe",
    "backgroundtaskhost.exe", "wudfhost.exe", "spoolsv.exe", "lsass.exe",
    "services.exe", "csrss.exe", "wininit.exe", "winlogon.exe",
    "steam.exe", "steamwebhelper.exe", "steamservice.exe", "gameoverlayui.exe",
    "epicgameslauncher.exe", "eosoverlayrenderer.exe", "ubisoftconnect.exe",
    "ubisoftgamelauncher.exe", "battle.net.exe", "galaxyclient.exe",
    "eadesktop.exe", "origin.exe", "riotclientservices.exe",
    "playnite.desktop.exe", "playnite.fullscreenapp.exe", "discord.exe",
    "bitdock.exe", "crashpad_handler.exe", "vcredist_x64.exe",
    "vcredist_x86.exe", "unins000.exe", "dxwebsetup.exe",
    "chrome.exe", "msedge.exe", "firefox.exe", "vlc.exe",
    "notepad.exe", "cmd.exe", "powershell.exe", NULL
};

static bool IsPlatform(const std::string& n)
{
    if (n.empty()) return false;
    for (int i = 0; kPlatform[i]; ++i)
        if (n == kPlatform[i]) return true;
    return n.find("crashpad") != std::string::npos ||
           n.find("vcredist") != std::string::npos ||
           n.find("unins") != std::string::npos;
}

static bool IsSystemClass(LPCWSTR c)
{
    static const wchar_t* k[] = {
        L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
        L"RainmeterMeterWindow", L"RainmeterTrayClass", L"tooltips_class32",
        L"MSCTFIME UI", L"IME", L"ForegroundStaging",
        L"ThumbnailDeviceHelperWnd", L"SIBTranslucentLayer", L"Button",
        L"NotifyIconOverflowWindow", L"SIBJumpView", NULL
    };
    for (int i = 0; k[i]; ++i)
        if (wcscmp(c, k[i]) == 0) return true;
    return wcsncmp(c, L"Shell_", 6) == 0;
}

/* One Z-order walk gives: pids owning a real visible window, and whether the
   frontmost substantial window is full-screen. All calls are local reads of
   window data (no cross-thread SendMessage). */
struct ScanCtx
{
    std::set<DWORD> visiblePids;
    double coverage;
    bool fullscreen;
    HWND fullscreenHwnd;
    bool sawSubstantial;
};

static BOOL CALLBACK ScanCB(HWND h, LPARAM p)
{
    ScanCtx* c = (ScanCtx*)p;
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    WCHAR cls[160] = { 0 };
    GetClassNameW(h, cls, 160);
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT r = { 0 };
    if (!GetWindowRect(h, &r)) return TRUE;
    int w = r.right - r.left, ht = r.bottom - r.top;

    if (!IsSystemClass(cls) && !(ex & WS_EX_TOOLWINDOW) &&
        w >= 200 && ht >= 120 && pid)
        c->visiblePids.insert(pid);

    /* The FRONTMOST substantial window alone decides full-screen status.
       Scanning several windows instead makes a permanently maximized browser
       (which is NOT full-screen) keep the taskbar stepped aside forever.
       A real game takes the foreground right after Launch, so it is the
       frontmost window by the time the 250ms layer tick runs. */
    if (c->sawSubstantial) return TRUE;
    if (IsSystemClass(cls) || (ex & WS_EX_TOOLWINDOW) ||
        !pid || w < 320 || ht < 240) return TRUE;

    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    HMONITOR hm = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    if (!hm || !GetMonitorInfoW(hm, &mi))
    {
        mi.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
        mi.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int mw = mi.rcMonitor.right - mi.rcMonitor.left;
    int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (mw <= 0 || mh <= 0) return TRUE;
    double area = (double)w * ht / ((double)mw * mh);
    if (area < 0.10) return TRUE;
    c->sawSubstantial = true;
    double cw = (double)w / mw, ch = (double)ht / mh;
    if (cw >= c->coverage && ch >= c->coverage)
    {
        c->fullscreen = true;
        c->fullscreenHwnd = h;
    }
    /* Keep walking: visiblePids must contain EVERY windowed process, not just
       those above the first substantial window (otherwise the launch-session
       gate can lose the game pid when another window sits higher in Z order). */
    return TRUE;
}

static ScanCtx Scan()
{
    ScanCtx s;
    s.coverage = g_coverage;
    s.fullscreen = false;
    s.fullscreenHwnd = NULL;
    s.sawSubstantial = false;
    EnumWindows(ScanCB, (LPARAM)&s);
    return s;
}

/* ------------------------------------------------------------- A) start gate */

static std::vector<DWORD> g_basePids;
static std::vector<DWORD> g_sessionPids;
static bool      g_sessionActive = false;
static bool      g_sessionCaptured = false;
static ULONGLONG g_sessionStart = 0;
static ULONGLONG g_sessionDeadline = 0;
static DWORD     g_lockedPid = 0;
static ULONGLONG g_releaseTick = 0;
static int       g_sessionCount = 0;
static bool      g_sessionGate = false;
static bool      g_fallbackGate = false;

static bool Contains(const std::vector<DWORD>& v, DWORD x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

static void EndSession()
{
    g_sessionActive = false;
    g_sessionCaptured = false;
    g_sessionPids.clear();
    g_basePids.clear();
    g_sessionGate = false;
}

static void ArmSession()
{
    std::vector<PEntry> s = Snapshot();
    g_basePids.clear();
    for (size_t i = 0; i < s.size(); ++i) g_basePids.push_back(s[i].pid);
    g_sessionPids.clear();
    g_sessionActive = true;
    g_sessionCaptured = false;
    g_sessionStart = GetTickCount64();
    g_sessionDeadline = g_sessionStart + g_sessionMs;
    g_sessionGate = false;
}

static void EvaluateGuard(const ScanCtx& scan)
{
    ULONGLONG now = GetTickCount64();
    std::vector<PEntry> snap = Snapshot();

    /* 1. Launch session: every process born since Launch. */
    if (g_sessionActive)
    {
        if (now < g_sessionDeadline)
        {
            for (size_t i = 0; i < snap.size(); ++i)
            {
                DWORD pid = snap[i].pid;
                if (!pid || Contains(g_basePids, pid) ||
                    Contains(g_sessionPids, pid) ||
                    IsPlatform(snap[i].name)) continue;
                if (scan.visiblePids.find(pid) == scan.visiblePids.end() &&
                    (now - g_sessionStart) >= 5000) continue;
                g_sessionPids.push_back(pid);
            }
        }
        for (size_t i = 0; i < g_sessionPids.size(); )
        {
            if (!PidAlive(snap, g_sessionPids[i]))
                g_sessionPids.erase(g_sessionPids.begin() + i);
            else ++i;
        }
        if (!g_sessionPids.empty()) g_sessionCaptured = true;
        g_sessionCount = (int)g_sessionPids.size();

        bool grace = (now - g_sessionStart) < 5000;
        g_sessionGate = false;
        for (size_t i = 0; i < g_sessionPids.size(); ++i)
            if (grace || scan.visiblePids.find(g_sessionPids[i]) != scan.visiblePids.end())
            { g_sessionGate = true; break; }

        if (!g_sessionGate && (g_sessionCaptured || now >= g_sessionDeadline))
            EndSession();
    }

    /* 2. Fallback for games started outside GameHUB. Purely local window
          queries - no SHQueryUserNotificationState / COM / cross-process
          synchronous calls that could stall the main thread. */
    g_fallbackGate = false;
    if (g_lockedPid)
    {
        /* Release when the process is gone OR no longer owns any real visible
           window. Without the second condition a long-lived host process
           (launcher / browser) could keep Start blocked forever. */
        if (PidAlive(snap, g_lockedPid) &&
            scan.visiblePids.find(g_lockedPid) != scan.visiblePids.end())
            g_fallbackGate = true;
        else { g_lockedPid = 0; g_releaseTick = now; }
    }
    HWND fg = GetForegroundWindow();
    HWND desk = GetDesktopWindow();
    HWND shellWnd = GetShellWindow();
    bool onDesktop = (!fg || fg == desk || (shellWnd && fg == shellWnd));
    std::string fgName;
    if (!onDesktop)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        fgName = NameForPid(snap, pid);
        if (!g_fallbackGate && !g_sessionGate && pid &&
            !IsPlatform(fgName) && scan.fullscreen &&
            !(g_releaseTick && (now - g_releaseTick) < 1000))
        {
            DWORD fsPid = 0;
            if (scan.fullscreenHwnd)
                GetWindowThreadProcessId(scan.fullscreenHwnd, &fsPid);
            if (fsPid && fsPid == pid)
            {
                g_lockedPid = pid;
                g_fallbackGate = true;
            }
        }
    }

    g_gameValue = (g_sessionGate || g_fallbackGate) ? 1.0 : 0.0;

    wchar_t buf[320];
    _snwprintf(buf, 319,
        L"GameHUBFix: game=%.0f session=%d pids=%d fallback=%d lock=%lu fg=%s "
        L"tray=%d settle=%d cycles=%d",
        g_gameValue, g_sessionActive ? 1 : 0, g_sessionCount,
        g_fallbackGate ? 1 : 0, (unsigned long)g_lockedPid,
        fgName.empty() ? L"(desktop)" : W(fgName).c_str(),
        g_trayActive ? 1 : 0, GetTickCount64() < g_settleDeadline ? 1 : 0,
        (int)g_cycles);
    buf[319] = 0;
    g_diag = buf;
}

/* ------------------------------------------- B) foreground / overlay Z-order */

static HWND g_targets[512];
static int  g_targetCount = 0;
static volatile LONG g_pulses = 0;

static std::string ProcNameOf(DWORD pid)
{
    if (!pid) return std::string();
    std::wstring full;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h)
    {
        WCHAR p[MAX_PATH]; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, p, &n)) full.assign(p, n);
        CloseHandle(h);
    }
    if (full.empty())
    {
        HANDLE h2 = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (h2)
        {
            WCHAR p[MAX_PATH];
            if (GetModuleBaseNameW(h2, NULL, p, MAX_PATH) > 0) full.assign(p);
            CloseHandle(h2);
        }
    }
    size_t i = full.find_last_of(L"\\/");
    std::wstring base = (i == std::wstring::npos) ? full : full.substr(i + 1);
    std::string out;
    for (size_t k = 0; k < base.size(); ++k)
    {
        wchar_t ch = base[k];
        if (ch >= L'A' && ch <= L'Z') ch = (wchar_t)(ch + 32);
        out.push_back(ch < 128 ? (char)ch : '?');
    }
    return Lower(out);
}

static bool IsOverlay(HWND h)
{
    WCHAR cls[160] = { 0 }, title[256] = { 0 };
    GetClassNameW(h, cls, 160);
    GetWindowTextW(h, title, 256);
    if (wcscmp(cls, L"RainmeterMeterWindow") == 0) return true;
    if (wcsstr(title, L"BitDock") != NULL) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return ProcNameOf(pid).find("bitdock") != std::string::npos;
}

static BOOL CALLBACK CollectOverlayCB(HWND h, LPARAM)
{
    if (!IsWindowVisible(h)) return TRUE;
    if (IsOverlay(h) && g_targetCount < 512) g_targets[g_targetCount++] = h;
    return TRUE;
}

static void InjectInputPulse()
{
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
}

static HWND g_rmWindow = NULL;
static BOOL CALLBACK PickRainmeterCB(HWND h, LPARAM)
{
    if (!IsWindowVisible(h)) return TRUE;
    WCHAR cls[128] = { 0 };
    GetClassNameW(h, cls, 128);
    if (wcscmp(cls, L"RainmeterMeterWindow") == 0)
    {
        g_rmWindow = h;
        return FALSE;
    }
    return TRUE;
}

static void ForceActivateRainmeter()
{
    g_rmWindow = NULL;
    EnumWindows(PickRainmeterCB, 0);
    if (!g_rmWindow) return;
    HWND fg = GetForegroundWindow();
    if (!fg || fg == g_rmWindow) return;
    DWORD fgTid = GetWindowThreadProcessId(fg, NULL);
    DWORD curTid = GetCurrentThreadId();
    BOOL attached = (fgTid && fgTid != curTid)
        ? AttachThreadInput(curTid, fgTid, TRUE) : FALSE;
    BringWindowToTop(g_rmWindow);
    SetForegroundWindow(g_rmWindow);
    if (attached) AttachThreadInput(curTid, fgTid, FALSE);
}

static void DoLaunch()
{
    InjectInputPulse();
    ForceActivateRainmeter();
    AllowSetForegroundWindow(ASFW_ANY);
    InterlockedIncrement(&g_pulses);

    ArmSession();

    g_targetCount = 0;
    EnumWindows(CollectOverlayCB, 0);
    g_settleDeadline = GetTickCount64() + g_settleMs;
    InterlockedIncrement(&g_launches);
}

/* ------------------------------------------------ C) taskbar strip Z-order */

struct TrayWin { HWND h; LONG ex; HWND prev; };
static TrayWin g_trayWins[32];
static int     g_trayCount = 0;
static DWORD   g_shellPid = 0;

static bool OverlapsAny(const RECT& r, const RECT* rects, int n)
{
    for (int i = 0; i < n; ++i)
        if (r.right > rects[i].left && r.left < rects[i].right &&
            r.bottom > rects[i].top && r.top < rects[i].bottom) return true;
    return false;
}

struct TrayRectCtx { RECT r[8]; int n; };

static BOOL CALLBACK TrayRectCB(HWND h, LPARAM p)
{
    TrayRectCtx* c = (TrayRectCtx*)p;
    WCHAR cls[128] = { 0 };
    GetClassNameW(h, cls, 128);
    if (wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 && c->n < 8)
        if (GetWindowRect(h, &c->r[c->n])) ++c->n;
    return TRUE;
}

struct StripCtx { DWORD pid; const RECT* rects; int n; };

static BOOL CALLBACK CollectStripCB(HWND h, LPARAM p)
{
    StripCtx* c = (StripCtx*)p;
    if (g_trayCount >= 32) return FALSE;
    if (!IsWindowVisible(h)) return TRUE;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (!(ex & WS_EX_TOPMOST)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid) return TRUE;
    RECT r = { 0 };
    if (!GetWindowRect(h, &r)) return TRUE;
    if (!OverlapsAny(r, c->rects, c->n)) return TRUE;
    g_trayWins[g_trayCount].h = h;
    g_trayWins[g_trayCount].ex = ex;
    g_trayWins[g_trayCount].prev = GetWindow(h, GW_HWNDPREV);
    ++g_trayCount;
    return TRUE;
}

static void PushBehind(HWND h, bool forceNotTopmost)
{
    if (!h || !IsWindow(h)) return;
    if (forceNotTopmost || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST))
        SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_ASYNC);
    SetWindowPos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_ASYNC);
}

static void BeginStepAside()
{
    TrayRectCtx tr;
    memset(&tr, 0, sizeof(tr));
    HWND primary = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!primary) return;
    g_shellPid = 0;
    GetWindowThreadProcessId(primary, &g_shellPid);
    if (!g_shellPid) return;
    if (GetWindowRect(primary, &tr.r[tr.n])) ++tr.n;
    EnumWindows(TrayRectCB, (LPARAM)&tr);

    g_trayCount = 0;
    StripCtx sc = { g_shellPid, tr.r, tr.n };
    EnumWindows(CollectStripCB, (LPARAM)&sc);
    if (!g_trayCount) return;
    for (int i = 0; i < g_trayCount; ++i) PushBehind(g_trayWins[i].h, true);
    g_trayActive = true;
    InterlockedIncrement(&g_begins);
}

static void RestoreTaskbar()
{
    if (!g_trayActive) return;
    for (int i = 0; i < g_trayCount; ++i)
    {
        HWND h = g_trayWins[i].h;
        if (!h || !IsWindow(h)) continue;
        SetWindowLongW(h, GWL_EXSTYLE, g_trayWins[i].ex);
        HWND prev = (g_trayWins[i].prev && IsWindow(g_trayWins[i].prev))
                  ? g_trayWins[i].prev : (HWND)HWND_TOPMOST;
        SetWindowPos(h, prev, 0, 0, 0, 0, SWP_ASYNC);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_ASYNC);
        if (prev != (HWND)HWND_TOPMOST)
            SetWindowPos(h, prev, 0, 0, 0, 0, SWP_ASYNC);
    }
    g_trayActive = false;
    g_trayCount = 0;
    InterlockedIncrement(&g_restores);
}

/* ------------------------------------------------------- main-thread tick */

static void LayerWork(const ScanCtx& scan, ULONGLONG now)
{
    if (g_enabled && scan.fullscreen)
    {
        if (!g_trayActive) BeginStepAside();
        else for (int i = 0; i < g_trayCount; ++i)
            PushBehind(g_trayWins[i].h, false);
    }
    else if (g_trayActive) RestoreTaskbar();

    if (now < g_settleDeadline)
    {
        LONG pushed = 0;
        for (int i = 0; i < g_targetCount; ++i)
        {
            HWND h = g_targets[i];
            if (!IsWindow(h)) continue;
            SetWindowPos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_ASYNC);
            ++pushed;
        }
        if (pushed) InterlockedExchangeAdd(&g_pushes, pushed);
    }
}

/* Called ONLY from Update() on Rainmeter's main thread. No locks, no waits. */
static void DoTick()
{
    ULONGLONG now = GetTickCount64();
    ScanCtx scan = Scan();
    EvaluateGuard(scan);                       /* gate: every single Update */
    if (g_lastLayerTick == 0 || now - g_lastLayerTick >= g_tickMs)
    {
        LayerWork(scan, now);                  /* layer: throttled */
        g_lastLayerTick = now;
    }
    InterlockedIncrement(&g_cycles);
}

/* -------------------------------------------------------------- config/io */

static void LoadConfig()
{
    WCHAR path[MAX_PATH] = { 0 };
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    std::wstring w(path);
    size_t i = w.find_last_of(L"\\/");
    std::wstring cfg = (i == std::wstring::npos ? std::wstring(L".\\")
                                                : w.substr(0, i + 1))
                     + L"GameHUBFix.ini";
    WCHAR val[64] = { 0 };
    GetPrivateProfileStringW(L"GameHUBFix", L"Enabled", L"1", val, 64, cfg.c_str());
    g_enabled = (_wtol(val) != 0);
    GetPrivateProfileStringW(L"GameHUBFix", L"TickMs", L"250", val, 64, cfg.c_str());
    long t = _wtol(val);
    if (t >= 100 && t <= 2000) g_tickMs = (DWORD)t;
    GetPrivateProfileStringW(L"GameHUBFix", L"SettleMs", L"10000", val, 64, cfg.c_str());
    t = _wtol(val);
    if (t >= 1000 && t <= 120000) g_settleMs = (DWORD)t;
    GetPrivateProfileStringW(L"GameHUBFix", L"SessionMs", L"90000", val, 64, cfg.c_str());
    t = _wtol(val);
    if (t >= 5000 && t <= 600000) g_sessionMs = (DWORD)t;
    GetPrivateProfileStringW(L"GameHUBFix", L"Coverage", L"0.98", val, 64, cfg.c_str());
    double cv = _wtof(val);
    if (cv >= 0.50 && cv <= 1.0) g_coverage = cv;
}

PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
    (void)rm;
    ++g_instances;
    LoadConfig();
    *data = new int(0);
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
{
    (void)data; (void)rm; (void)maxValue;
    LoadConfig();
    /* Deliberately NOT clearing the session or the latch: refreshing a skin
       must never un-block Start in the middle of a running game. */
}

PLUGIN_EXPORT double Update(void* data)
{
    (void)data;
    DoTick();
    return g_gameValue;
}

PLUGIN_EXPORT LPCWSTR GetString(void* data)
{
    (void)data;
    return g_diag.empty() ? L"GameHUBFix: idle" : g_diag.c_str();
}

PLUGIN_EXPORT void ExecuteBang(void* data, LPCWSTR args)
{
    (void)data;
    if (!args) return;
    std::wstring a(args);
    size_t b = 0, e = a.size();
    while (b < e && (a[b] == L'\"' || a[b] == L' ' || a[b] == L'\t')) ++b;
    while (e > b && (a[e-1] == L'\"' || a[e-1] == L' ' || a[e-1] == L'\t')) --e;
    std::wstring cmd = a.substr(b, e - b);
    for (size_t i = 0; i < cmd.size(); ++i)
        if (cmd[i] >= L'A' && cmd[i] <= L'Z') cmd[i] = (wchar_t)(cmd[i] + 32);

    if (cmd == L"launch") DoLaunch();
    else if (cmd == L"pulse")
    {
        InjectInputPulse();
        ForceActivateRainmeter();
        AllowSetForegroundWindow(ASFW_ANY);
        InterlockedIncrement(&g_pulses);
    }
    else if (cmd == L"arm") ArmSession();
    else if (cmd == L"settle")
    {
        g_targetCount = 0;
        EnumWindows(CollectOverlayCB, 0);
        g_settleDeadline = GetTickCount64() + g_settleMs;
    }
    else if (cmd == L"stop")
    {
        g_settleDeadline = 0;
        RestoreTaskbar();
        EndSession();
        g_lockedPid = 0;
    }
    else if (cmd == L"reset" || cmd == L"unblock")
    {
        EndSession();
        g_lockedPid = 0;
        g_releaseTick = 0;
    }
    else if (cmd == L"restore") RestoreTaskbar();
    else if (cmd == L"disable") { g_enabled = false; RestoreTaskbar(); }
    else if (cmd == L"enable") { g_enabled = true; }
    else if (cmd == L"reload") { LoadConfig(); }
}

PLUGIN_EXPORT void Finalize(void* data)
{
    --g_instances;
    if (g_instances <= 0) RestoreTaskbar();   /* never leave tray un-topmost */
    delete (int*)data;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hInst = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}
