/* Standalone ABI probe for GameHUBFix.dll, without Rainmeter.

   Proves the generic launch-session gate:
     Launch -> start a brand-new windowed process with an arbitrary name
            -> gate must become 1 (Start blocked)
     kill that process -> gate must return to 0 (Start works again)

   No game name is ever given to the plugin. */
#include <windows.h>
#include <cstdio>
#include <string>

typedef void (*PInit)(void**, void*);
typedef void (*PReload)(void*, void*, double*);
typedef double (*PUpdate)(void*);
typedef LPCWSTR (*PString)(void*);
typedef void (*PBang)(void*, LPCWSTR);
typedef void (*PFinal)(void*);

static void Wait(int ms) { Sleep(ms); }

int wmain(int argc, wchar_t** argv)
{
    LPCWSTR dll = argc > 1 ? argv[1] : L"GameHUBFix.dll.new";
    LPCWSTR exe = argc > 2 ? argv[2] : L"SessionGameTest.exe";

    HMODULE h = LoadLibraryW(dll);
    if (!h) { printf("FAIL: cannot load %ls (err=%lu)\n", dll, GetLastError()); return 1; }
    PInit    pInit    = (PInit)GetProcAddress(h, "Initialize");
    PReload  pReload  = (PReload)GetProcAddress(h, "Reload");
    PUpdate  pUpdate  = (PUpdate)GetProcAddress(h, "Update");
    PString  pString  = (PString)GetProcAddress(h, "GetString");
    PBang    pBang    = (PBang)GetProcAddress(h, "ExecuteBang");
    PFinal   pFinal   = (PFinal)GetProcAddress(h, "Finalize");
    if (!pInit || !pReload || !pUpdate || !pString || !pBang || !pFinal)
    { printf("FAIL: missing export\n"); return 1; }

    void* m = 0;
    double mx = 0;
    pInit(&m, 0);
    pReload(m, 0, &mx);

    printf("baseline            : value=%.0f  %ls\n", pUpdate(m), pString(m));

    pBang(m, L"Launch");
    printf("after Launch        : value=%.0f  %ls\n", pUpdate(m), pString(m));

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cmd = std::wstring(L"\"") + exe + L"\"";
    if (!CreateProcessW(0, (LPWSTR)cmd.c_str(), 0, 0, FALSE, 0, 0, 0, &si, &pi))
    {
        printf("FAIL: cannot start %ls (err=%lu)\n", exe, GetLastError());
        pFinal(m); FreeLibrary(h); return 1;
    }
    printf("started %ls pid=%lu (arbitrary name, no game list)\n", exe, pi.dwProcessId);

    double blocked = -1;
    for (int i = 0; i < 15; ++i)
    {
        Wait(200);
        double v = pUpdate(m);
        if (i < 3 || v == 1.0)
            printf("t=%.1fs              : value=%.0f  %ls\n", (i + 1) * 0.2, v, pString(m));
        if (v == 1.0) { blocked = v; break; }
    }

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("process terminated\n");

    double released = -1;
    for (int i = 0; i < 15; ++i)
    {
        Wait(200);
        double v = pUpdate(m);
        if (i < 3 || v == 0.0)
            printf("t=%.1fs              : value=%.0f  %ls\n", (i + 1) * 0.2, v, pString(m));
        if (v == 0.0) { released = v; break; }
    }

    pFinal(m);
    FreeLibrary(h);

    printf("RESULT generic-block=%.0f auto-release=%.0f -> %s\n",
           blocked, released,
           (blocked == 1.0 && released == 0.0) ? "PASS" : "FAIL");
    return (blocked == 1.0 && released == 0.0) ? 0 : 1;
}
