/* Dummy "game": a normal windowed app with an arbitrary name.
   Used to prove the gate is generic and does not match exe names. */
#include <windows.h>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"SessionGameTestWindowClass";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, L"SessionGameTestWindowClass",
                             L"Session Game Test",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             120, 120, 800, 600, 0, 0, hInst, 0);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
