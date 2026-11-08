"""GameHUBFix v2 regression: the Rainmeter main thread must NEVER stall.

Simulates the exact field sequence (Start -> launch game -> full-screen window
-> game exits -> taskbar restore) plus repeated skin refreshes, while timing
every single Update() call. A synchronous cross-process SetWindowPos or a
lock wait would show up as a multi-hundred-ms / infinite Update() here.
"""
import ctypes
import ctypes.wintypes as w
import sys
import time

DLL = sys.argv[1] if len(sys.argv) > 1 else "GameHUBFix.dll.new"

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
u.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                              ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_int, w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID]
u.CreateWindowExW.restype = w.HWND
u.RegisterClassW.argtypes = [ctypes.c_void_p]
u.SetLayeredWindowAttributes.argtypes = [w.HWND, w.COLORREF, w.BYTE, w.DWORD]
u.BringWindowToTop.argtypes = [w.HWND]
u.SetForegroundWindow.argtypes = [w.HWND]
u.PeekMessageW.argtypes = [ctypes.c_void_p, w.HWND, w.UINT, w.UINT, w.UINT]
u.TranslateMessage.argtypes = [ctypes.c_void_p]
u.DispatchMessageW.argtypes = [ctypes.c_void_p]
k.GetModuleHandleW.argtypes = [w.LPCWSTR]
k.GetModuleHandleW.restype = w.HINSTANCE

WNDPROC = ctypes.WINFUNCTYPE(ctypes.c_longlong, w.HWND, w.UINT, w.WPARAM, w.LPARAM)


class WNDCLASSW(ctypes.Structure):
    _fields_ = [("style", w.UINT), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", ctypes.c_void_p), ("hIcon", ctypes.c_void_p),
                ("hCursor", ctypes.c_void_p), ("hbrBackground", ctypes.c_void_p),
                ("lpszMenuName", w.LPCWSTR), ("lpszClassName", w.LPCWSTR)]


def wndproc(hw, msg, wp, lp):
    return u.DefWindowProcW(hw, msg, wp, lp)


SWP = 0x0001 | 0x0002 | 0x0010 | 0x0200 | 0x0400
WS_EX_LAYERED = 0x80000
WS_EX_TOPMOST = 0x8
WS_POPUP = 0x80000000
WS_VISIBLE = 0x10000000
LWA_ALPHA = 0x2
HWND_TOP = 0
HWND_TOPMOST = -1
GW_HWNDPREV = 3

u.GetSystemMetrics.argtypes = [ctypes.c_int]
SW, SH = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
print("screen: %dx%d" % (SW, SH))

hinst = k.GetModuleHandleW(None)
wc = WNDCLASSW()
wc.lpfnWndProc = WNDPROC(wndproc)
wc.hInstance = hinst
wc.lpszClassName = "GHFStressE2E"
u.RegisterClassW(ctypes.byref(wc))


def make_fullscreen():
    hw = u.CreateWindowExW(WS_EX_LAYERED, "GHFStressE2E", "stress game",
                           WS_POPUP | WS_VISIBLE, 0, 0, SW, SH,
                           0, 0, hinst, None)
    u.SetLayeredWindowAttributes(hw, 0, 0, LWA_ALPHA)
    u.SetWindowPos(hw, HWND_TOP, 0, 0, 0, 0, SWP)
    # A real game takes the foreground right after Launch; do the same here
    # so the Z-order reflects the field instead of leaving a browser on top.
    u.BringWindowToTop(hw)
    u.SetForegroundWindow(hw)
    return hw


def drain():
    msg = w.MSG()
    while u.PeekMessageW(ctypes.byref(msg), 0, 0, 0, 1):
        u.TranslateMessage(ctypes.byref(msg))
        u.DispatchMessageW(ctypes.byref(msg))


def pump(m, seconds, lat):
    states = []
    end = time.time() + seconds
    while time.time() < end:
        drain()
        t0 = time.perf_counter()
        h.Update(m)
        lat.append((time.perf_counter() - t0) * 1000.0)
        states.append(bool(u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST))
        time.sleep(0.02)
    return states


tray = u.FindWindowW("Shell_TrayWnd", None)
orig_ex = u.GetWindowLongW(tray, -20)
orig_prev = u.GetWindow(tray, GW_HWNDPREV)
print("baseline taskbar topmost=%s" % bool(orig_ex & WS_EX_TOPMOST))

m = ctypes.c_void_p()
mx = ctypes.c_double()
h.Initialize(ctypes.byref(m), None)
h.Reload(m, None, ctypes.byref(mx))

lat = []
aside_ok = 0
restore_ok = 0
rounds = 6
try:
    for r in range(rounds):
        h.ExecuteBang(m, "Launch")           # Start -> game launch
        full = make_fullscreen()
        states = pump(m, 0.55, lat)
        aside_samples = sum(1 for s in states if not s)
        if aside_samples:
            aside_ok += 1
        print("round %d: aside samples=%d/%d  diag=%s"
              % (r, aside_samples, len(states), h.GetString(m)))
        u.DestroyWindow(full)                # game exits
        pump(m, 0.75, lat)
        if u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST:
            restore_ok += 1
        # simulate !RefreshApp / skin reload
        h.Finalize(m)
        m = ctypes.c_void_p()
        h.Initialize(ctypes.byref(m), None)
        h.Reload(m, None, ctypes.byref(mx))
    print("diag: %s" % h.GetString(m))
finally:
    if not (u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST):
        print("force-restoring taskbar in finally")
        u.SetWindowLongW(tray, -20, orig_ex)
        u.SetWindowPos(tray, orig_prev, 0, 0, 0, 0, SWP)
        u.SetWindowPos(tray, HWND_TOPMOST, 0, 0, 0, 0, SWP)
        u.SetWindowPos(tray, orig_prev, 0, 0, 0, 0, SWP)
    h.Finalize(m)

lat_sorted = sorted(lat)
peak = lat_sorted[-1] if lat_sorted else 0.0
p99 = lat_sorted[int(len(lat_sorted) * 0.99)] if lat_sorted else 0.0
final_topmost = bool(u.GetWindowLongW(tray, -20) & WS_EX_TOPMOST)
print("Update() calls=%d  peak=%.2fms  p99=%.2fms" % (len(lat), peak, p99))
print("step_aside=%d/%d  restored=%d/%d  final_topmost=%s"
      % (aside_ok, rounds, restore_ok, rounds, final_topmost))
ok = (aside_ok == rounds and restore_ok == rounds and final_topmost and peak < 100.0)
print("RESULT -> %s" % ("PASS" if ok else "FAIL"))
sys.exit(0 if ok else 1)
