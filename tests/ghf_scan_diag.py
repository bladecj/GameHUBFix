"""Dump the Z-order the way ScanCB sees it, with and without a Launch bang."""
import ctypes
import ctypes.wintypes as w
import sys

DLL = sys.argv[1] if len(sys.argv) > 1 else "GameHUBFix.dll.new"
u = ctypes.windll.user32
k = ctypes.windll.kernel32

u.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                              ctypes.c_int, ctypes.c_int, ctypes.c_int,
                              ctypes.c_int, w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID]
u.CreateWindowExW.restype = w.HWND
u.RegisterClassW.argtypes = [ctypes.c_void_p]
u.DefWindowProcW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                           ctypes.c_int, ctypes.c_int, w.UINT]
u.GetWindowLongW.argtypes = [w.HWND, ctypes.c_int]
u.GetWindowLongW.restype = ctypes.c_long
k.GetModuleHandleW.argtypes = [w.LPCWSTR]
k.GetModuleHandleW.restype = w.HINSTANCE
u.GetSystemMetrics.argtypes = [ctypes.c_int]
SW, SH = u.GetSystemMetrics(0), u.GetSystemMetrics(1)
print("screen: %dx%d" % (SW, SH))

WNDPROC = ctypes.WINFUNCTYPE(ctypes.c_longlong, w.HWND, w.UINT, w.WPARAM, w.LPARAM)


class WNDCLASSW(ctypes.Structure):
    _fields_ = [("style", w.UINT), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", ctypes.c_void_p), ("hIcon", ctypes.c_void_p),
                ("hCursor", ctypes.c_void_p), ("hbrBackground", ctypes.c_void_p),
                ("lpszMenuName", w.LPCWSTR), ("lpszClassName", w.LPCWSTR)]


def wndproc(hw, msg, wp, lp):
    return u.DefWindowProcW(hw, msg, wp, lp)


SYSTEM = {"Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd",
          "RainmeterMeterWindow", "RainmeterTrayClass", "tooltips_class32",
          "MSCTFIME UI", "IME", "ForegroundStaging",
          "ThumbnailDeviceHelperWnd", "SIBTranslucentLayer", "Button",
          "NotifyIconOverflowWindow", "SIBJumpView"}

hinst = k.GetModuleHandleW(None)
wc = WNDCLASSW()
wc.lpfnWndProc = WNDPROC(wndproc)
wc.hInstance = hinst
wc.lpszClassName = "GHFScanDiag"
u.RegisterClassW(ctypes.byref(wc))

h = ctypes.CDLL(DLL)
h.Initialize.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]
h.Reload.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_double)]
h.Update.argtypes = [ctypes.c_void_p]
h.Update.restype = ctypes.c_double
h.GetString.argtypes = [ctypes.c_void_p]
h.GetString.restype = ctypes.c_wchar_p
h.ExecuteBang.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
h.Finalize.argtypes = [ctypes.c_void_p]
m = ctypes.c_void_p()
mx = ctypes.c_double()
h.Initialize(ctypes.byref(m), None)
h.Reload(m, None, ctypes.byref(mx))

if len(sys.argv) > 2 and sys.argv[2] == "launch":
    h.ExecuteBang(m, "Launch")
    print(">>> Launch bang issued")

full = u.CreateWindowExW(0x80000, "GHFScanDiag", "fullscreen",
                         0x80000000 | 0x10000000, 0, 0, SW, SH,
                         0, 0, hinst, None)
u.SetWindowPos(full, 0, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)

CB = ctypes.WINFUNCTYPE(ctypes.c_bool, w.HWND, w.LPARAM)
rows = []


def cb(hw, lp):
    cls = ctypes.create_unicode_buffer(160)
    u.GetClassNameW(hw, cls, 160)
    t = ctypes.create_unicode_buffer(256)
    u.GetWindowTextW(hw, t, 256)
    r = w.RECT()
    u.GetWindowRect(hw, ctypes.byref(r))
    ex = u.GetWindowLongW(hw, -20)
    pid = w.DWORD()
    u.GetWindowThreadProcessId(hw, ctypes.byref(pid))
    rows.append((hw, cls.value, t.value[:28], u.IsWindowVisible(hw),
                 u.IsIconic(hw), ex, pid.value,
                 r.right - r.left, r.bottom - r.top))
    return True


u.EnumWindows(CB(cb), 0)

print("screen=%dx%d  total top-level=%d"
      % (u.GetSystemMetrics(0), u.GetSystemMetrics(1), len(rows)))
print("%3s %-26s %-8s %-4s %-4s %-9s %7s %s"
      % ("idx", "class", "vis", "icon", "top", "tool", "size", "note"))
first = True
for i, (hw, cls, title, vis, icon, ex, pid, ww, hh) in enumerate(rows[:28]):
    syscls = cls in SYSTEM or cls.startswith("Shell_")
    tool = bool(ex & 0x80)
    top = bool(ex & 0x8)
    substantial = (not syscls and not tool and pid and ww >= 320 and hh >= 240
                   and vis and not icon)
    note = ""
    if substantial and first:
        cw, ch = ww / float(SW), hh / float(SH)
        note = "FIRST SUBSTANTIAL fullscreen=%s (cw=%.2f ch=%.2f)" % (
            "YES" if cw >= 0.98 and ch >= 0.98 else "NO", cw, ch)
        first = False
    elif substantial:
        note = "substantial"
    print("%3d %-26s %-8s %-4s %-4s %-9s %4dx%-4d %s"
          % (i, cls[:26], vis, icon, int(top), int(tool), ww, hh, note))

# Replicate ScanCB exactly and report what the plugin would decide.
seen = False
fullscreen = False
first_row = None
vispids = set()
for (hw, cls, title, vis, icon, ex, pid, ww, hh) in rows:
    if not vis or icon:
        continue
    syscls = cls in SYSTEM or cls.startswith("Shell_")
    tool = bool(ex & 0x80)
    if not syscls and not tool and ww >= 200 and hh >= 120 and pid:
        vispids.add(pid)
    if seen:
        continue
    if syscls or tool or not pid or ww < 320 or hh < 240:
        continue
    if ww * hh / (float(SW) * SH) < 0.10:
        continue
    seen = True
    first_row = (hw, cls, title, pid, ww, hh)
    cw, ch = ww / float(SW), hh / float(SH)
    if cw >= 0.98 and ch >= 0.98:
        fullscreen = True

print("\nScanCB replication:")
print("  first substantial : %s" % (first_row,))
print("  fullscreen        : %s   (coverage threshold 0.98)" % fullscreen)
print("  visiblePids       : %d" % len(vispids))

# CollectStripCB candidates
tray = u.FindWindowW("Shell_TrayWnd", None)
tr = w.RECT()
u.GetWindowRect(tray, ctypes.byref(tr))
shellpid = w.DWORD()
u.GetWindowThreadProcessId(tray, ctypes.byref(shellpid))
strip = [(i, cls) for i, (hw, cls, t, vis, icon, ex, pid, ww, hh) in enumerate(rows)
         if vis and (ex & 0x8) and pid == shellpid.value
         and hw != 0 and cls in ("Shell_TrayWnd", "Button", "SIBTranslucentLayer")]
print("  taskbar strip cands: %s" % strip)
print("plugin diag: %s" % h.GetString(m))
print("after Update: value=%.0f  %s" % (h.Update(m), h.GetString(m)))
h.Finalize(m)
