"""Standalone end-to-end test for the LAYER half of GameHUBFix.dll.

Loads the DLL directly (no Rainmeter needed), drives Update() the same way
Rainmeter does, creates a fully transparent full-screen window and checks that
the taskbar strip steps aside and is restored exactly.

Safety notes:
  * Screen size is read at runtime (works on 1080p streaming / 4K / multi-monitor).
  * If the taskbar is ALREADY stepped aside when the test starts (e.g. left
    over from a streaming session), it is normalized first. Otherwise the test
    would save a wrong baseline and report a false FAIL.
  * ONLY the three known taskbar-strip windows are touched. Progman / WorkerW
    are never modified.
  * A finally block always leaves the taskbar topmost again.
"""
import ctypes
import ctypes.wintypes as w
import sys
import time

DLL = sys.argv[1] if len(sys.argv) > 1 else "GameHUBFix.dll"

h = ctypes.CDLL(DLL)
h.Initialize.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]
h.Reload.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_double)]
h.Update.argtypes = [ctypes.c_void_p]
h.Update.restype = ctypes.c_double
h.GetString.argtypes = [ctypes.c_void_p]
h.GetString.restype = ctypes.c_wchar_p
h.ExecuteBang.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
h.Finalize.argtypes = [ctypes.c_void_p]

u = ctypes.windll.user32
k = ctypes.windll.kernel32
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.GetWindowLongW.argtypes = [w.HWND, ctypes.c_int]
u.GetWindowLongW.restype = ctypes.c_long
u.SetWindowLongW.argtypes = [w.HWND, ctypes.c_int, ctypes.c_long]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                           ctypes.c_int, ctypes.c_int, w.UINT]
u.GetWindow.argtypes = [w.HWND, w.UINT]
u.GetWindow.restype = w.HWND
u.DefWindowProcW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SetLayeredWindowAttributes.argtypes = [w.HWND, w.COLORREF, w.BYTE, w.DWORD]
u.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                              ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_int, w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID]
u.CreateWindowExW.restype = w.HWND
u.RegisterClassW.argtypes = [ctypes.c_void_p]
u.PeekMessageW.argtypes = [ctypes.c_void_p, w.HWND, w.UINT, w.UINT, w.UINT]
u.GetSystemMetrics.argtypes = [ctypes.c_int]
k.GetModuleHandleW.argtypes = [w.LPCWSTR]
k.GetModuleHandleW.restype = w.HINSTANCE

WNDPROC = ctypes.WINFUNCTYPE(ctypes.c_longlong, w.HWND, w.UINT, w.WPARAM, w.LPARAM)


class WNDCLASSW(ctypes.Structure):
    _fields_ = [("style", w.UINT), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", ctypes.c_void_p), ("hIcon", ctypes.c_void_p),
                ("hCursor", ctypes.c_void_p), ("hbrBackground", ctypes.c_void_p),
                ("lpszMenuName", w.LPCWSTR), ("lpszClassName", w.LPCWSTR)]


def wndproc(hw, m, wp, lp):
    return u.DefWindowProcW(hw, m, wp, lp)


SWP = 0x0001 | 0x0002 | 0x0010 | 0x0200 | 0x0400   # NOSIZE|NOMOVE|NOACTIVATE|NOOWNERZORDER|NOSENDCHANGING
WS_EX_LAYERED = 0x80000
WS_EX_TOPMOST = 0x8
WS_POPUP = 0x80000000
WS_VISIBLE = 0x10000000
LWA_ALPHA = 0x2
HWND_TOP = 0
HWND_TOPMOST = -1
HWND_NOTOPMOST = -2
GW_HWNDPREV = 3

SW = u.GetSystemMetrics(0)
SH = u.GetSystemMetrics(1)
print("screen: %dx%d" % (SW, SH))

STRIP_CLASSES = ("Button", "Shell_TrayWnd", "SIBTranslucentLayer")


def strip_windows():
    out = []
    for cls in STRIP_CLASSES:
        hw = u.FindWindowW(cls, None)
        if hw:
            out.append((hw, cls, u.GetWindowLongW(hw, -20)))
    return out


def force_topmost():
    """Put the three strip windows back into the real topmost group.

    SetWindowPos(HWND_TOPMOST) is what Windows uses to move a window into the
    topmost band; merely setting the WS_EX_TOPMOST style bit with
    SetWindowLongW leaves a stale flag that does not match the actual Z order.
    """
    for hw, cls, ex in strip_windows():
        u.SetWindowPos(hw, HWND_TOPMOST, 0, 0, 0, 0, SWP)
    return bool(u.GetWindowLongW(u.FindWindowW("Shell_TrayWnd", None), -20) & WS_EX_TOPMOST)


m = ctypes.c_void_p()
mx = ctypes.c_double()
h.Initialize(ctypes.byref(m), None)
h.Reload(m, None, ctypes.byref(mx))

tray = u.FindWindowW("Shell_TrayWnd", None)
baseline = bool(u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST)
print("baseline: taskbar topmost=%s" % baseline)
if not baseline:
    print("baseline was stepped aside by another process -> normalizing first")
    force_topmost()
    print("normalized: taskbar topmost=%s" %
          bool(u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST))

hinst = k.GetModuleHandleW(None)
wc = WNDCLASSW()
wc.lpfnWndProc = WNDPROC(wndproc)
wc.hInstance = hinst
wc.lpszClassName = "GHFLayerE2E"
u.RegisterClassW(ctypes.byref(wc))
full = u.CreateWindowExW(WS_EX_LAYERED, "GHFLayerE2E", "fullscreen probe",
                         WS_POPUP | WS_VISIBLE, 0, 0, SW, SH,
                         0, 0, hinst, None)
u.SetLayeredWindowAttributes(full, 0, 0, LWA_ALPHA)
u.SetWindowPos(full, HWND_TOP, 0, 0, 0, 0, SWP)
# A real game takes the foreground right after launch. Without this, a
# maximized browser can remain the frontmost substantial window and the
# plugin's single-window full-screen test never sees the probe window.
u.BringWindowToTop(full)
u.SetForegroundWindow(full)

aside = False
restored = False
try:
    for i in range(20):
        time.sleep(0.2)
        msg = w.MSG()
        while u.PeekMessageW(ctypes.byref(msg), 0, 0, 0, 1):
            u.TranslateMessage(ctypes.byref(msg))
            u.DispatchMessageW(ctypes.byref(msg))
        h.Update(m)
        if not (u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST):
            aside = True
            print("t=%.1fs STEP ASIDE: topmost=False   %s" % (i * 0.2, h.GetString(m)))
            break
    u.DestroyWindow(full)
    full = None
    for i in range(20):
        time.sleep(0.2)
        h.Update(m)
        if u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST:
            restored = True
            print("t=%.1fs RESTORED: topmost=True    %s" % (i * 0.2, h.GetString(m)))
            break
finally:
    if full:
        u.DestroyWindow(full)
    # Never leave the user's taskbar stepped aside.
    force_topmost()
    h.Finalize(m)

print("RESULT step_aside=%s restored=%s -> %s"
      % (aside, restored, "PASS" if aside and restored else "FAIL"))
sys.exit(0 if (aside and restored) else 1)
