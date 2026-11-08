# 技术说明

本文解释插件为什么要这么写。面向维护者；只想使用请读 [README.md](../README.md)。

---

## 1. 为什么手柄启动和鼠标点击启动不一样

手柄按键由 `XInputGetState()` **轮询**得到，Rainmeter 的 XInput 插件在自己的更新周期里读到 Start 状态变化，然后执行一个 bang 启动游戏。这条链路里没有任何东西进入 Windows 的**输入队列**。

鼠标点击则不同：真实的鼠标消息会刷新 `LASTINPUTINFO`、触发前台锁（foreground lock）的解除，Windows shell 因此能完成「新进程成为前台窗口 → 桌面 overlay 与任务栏让位」这一整套交接。手柄路径跳过了这一步，于是：

- 游戏窗口拿不到前台，被留在 Z 序底部，和桌面皮肤交错；
- shell 没有检测到「全屏前台应用」，不会把任务栏从 topmost 组让出去；
- 实测该机器的 `SPI_GETFOREGROUNDLOCKTIMEOUT` 为 `0x7FFFFFFF`，前台锁异常长，进一步放大了这个差异。

`Launch` 里的处理就是补上这一下：ALT 按下/抬起脉冲（产生真实输入事件）、`AttachThreadInput` + `SetForegroundWindow` 让 Rainmeter 线程临时取得前台资格、`AllowSetForegroundWindow(ASFW_ANY)` 放行随后启动的游戏进程。之后 `Settle` 在 `SettleMs` 内每 250 ms 把桌面 overlay 重新压到 `HWND_BOTTOM`，覆盖慢启动游戏的窗口创建时序。

Alt+Tab 一次之所以能「修好」，也是因为手动触发了同一次前台交接。

---

## 2. 为什么旧的 `IsFullScreen` 门控反复失效

`PluginIsFullScreen` 的核心判据是前台窗口矩形与桌面矩形 `EqualRect` 精确相等，并返回前台进程名。实际使用中：

- 独占全屏（真正的 exclusive fullscreen）经常没有普通意义上的前台窗口，插件返回空字符串，而旧配置的白名单里恰好有 `^$`（匹配空串）→ 被判成「在桌面」，Start 放行；
- 窗口化/无边框全屏、多显示器、任务栏自动隐藏都会让矩形不精确相等；
- 旧 `Dock.ini` 的门控是 `IfNotMatchAction=[!DisableMeasure "GamepadActivation"]...`：`!DisableMeasure` 是**粘性**的，游戏退出后 measure 不会自己恢复；要等 `UpdateDivider=10` × `Update=1000` = **10 秒** 的下一次轮询，且前台进程必须正好命中一个只有 3 项的白名单才执行 `!EnableMeasure`。这正是「点一下桌面后还要等挺久」的来源；
- 不同游戏表现不一致，也都源于上述两点，而不是 HotKey.dll 或 XInput.dll 有问题——这两个原版 DLL 从未修改过。

v2 的门控完全不看前台窗口矩形，只看进程存活与窗口可见性。

---

## 3. 三层 Z 序

一次 `EnumWindows` 从顶到底遍历，能看到三种角色：

| 层 | 典型窗口类 | 处理方式 |
| --- | --- | --- |
| topmost 组 | `Button`（开始按钮）、`Shell_TrayWnd`、`SIBTranslucentLayer`（都属于 explorer.exe） | 全屏时临时摘掉 `WS_EX_TOPMOST` 并压到 `HWND_BOTTOM`；退出后按保存的 `GWL_EXSTYLE` 与 `GW_HWNDPREV` 精确还原 |
| 非 topmost 桌面 overlay | `RainmeterMeterWindow`、BitDock 窗口 | `Settle` 期间压到 `HWND_BOTTOM` |
| 游戏窗口 | 各种 | 不动；让它自然成为最前的非 topmost 窗口 |

关键认识：**只压皮肤是不够的**。任务栏属于 topmost 组，无论把非 topmost 窗口压得多低，topmost 组永远画在它们上面。这就是「皮肤已经退到游戏后面，但底部任务栏还在游戏前面」的原因。

`BeginStepAside` 的采集条件很严格，避免误伤：

1. 先取 `Shell_TrayWnd` 与所有 `Shell_SecondaryTrayWnd` 的矩形；
2. 只收 **explorer.exe 拥有**、**当前是 `WS_EX_TOPMOST`**、且**与上述矩形重叠**的可见窗口；
3. 每个窗口保存 `GWL_EXSTYLE` 与 `GW_HWNDPREV`；
4. 让位期间每个 tick 重申一次压底（防止 shell 自己拉回）；
5. 全屏窗口消失后逆序还原。

`IsSystemClass()` 里把 `Progman`、`WorkerW`、`Shell_*`、`RainmeterMeterWindow`、IME、tooltip 等排除在「实质窗口」之外，避免桌面/输入法窗口干扰全屏判定。

---

## 4. 死锁复盘（v1 → v2）

v1 有一个 timer-queue 回调 `Tick()`，和 Rainmeter 主线程共享一把 `CRITICAL_SECTION`。

```
timer 线程:  EnterCriticalSection(g_cs)
             └─ BeginStepAside / RestoreTaskbar / PushBehind
                  └─ SetWindowPos(explorer 的任务栏窗口)   ← 同步跨进程调用
                       └─ 游戏退出瞬间 shell 正在重建任务栏 → 阻塞，不返回
                          ⇒ g_cs 一直不放

Rainmeter 主线程:  Update() / Finalize()
                   └─ EnterCriticalSection(g_cs)  ⇒ 永久等待
                      ⇒ 整个界面无响应，只能任务管理器强杀
```

第一处互等（`Finalize` 持有锁并 `DeleteTimerQueueTimer(..., INVALID_HANDLE_VALUE)` 等待回调，回调又在等锁）曾单独修过，但没覆盖第二条路径，于是用户实测「关闭游戏后 Rainmeter 直接卡死」。

v2 的做法是从结构上消除死锁面，而不是继续补锁：

- 删掉 timer、临界区、DLL 自引用、`SHQueryUserNotificationState`（COM 调用也可能跨进程阻塞）；
- 所有工作由 `Update()` 在 Rainmeter 主线程执行（`PadListener.ini` 的 `Update=100`）；门控每次都算，层级工作按 `TickMs` 节流；
- **每个 `SetWindowPos` 都带 `SWP_ASYNCWINDOWPOS`**：主线程只把请求投递出去，永不等待 explorer；
- `Finalize` 只做一次 `RestoreTaskbar()`，不等待任何回调；
- 皮肤刷新（`Reload`）**不清理**会话与 latch，避免在玩游戏时误放行 Start。

`tests/ghf_v2_stress.py` 会连续调用 `Update()` 并计时：同步 `SetWindowPos` 或锁等待会立刻表现为几百毫秒甚至无限长的 `Update()`。实测 283 次调用峰值约 16.6 ms。

---

## 5. 门控状态机

```
[Launch]
  └─ 拍进程快照 baseline（PID 集合）
  └─ sessionActive = true, deadline = now + SessionMs

每个 Update():
  1) 扫描 Z 序 → visiblePids（所有拥有实质可见窗口的 PID）
  2) 进程快照里「不在 baseline、不在 sessionPids、不是平台进程」的新 PID：
        · 前 5 秒宽限期：直接收（游戏进程还没来得及建窗口）
        · 5 秒后：必须已在 visiblePids 里才收
  3) 清掉已退出的 PID
  4) 只要 sessionPids 里还有「存活且拥有可见窗口」的进程 → game = 1
  5) 全部消失（或超过 deadline 且从未捕获到）→ 结束会话，game = 0

兜底（不经 GameHUB 启动的游戏）:
  前台窗口非桌面 + 首个实质窗口覆盖显示器 ≥ Coverage
  + 两者属于同一 PID + 该 PID 不是平台进程
  → 锁住该 PID；进程消失或不再有可见窗口即释放
```

`kPlatform[]` 只用于排除开机/平台就在跑的辅助进程（Steam、Epic、浏览器、系统服务、Rainmeter、BitDock 等），**不是游戏名单**。它是稳定集合，不需要为新游戏维护。

历史上曾按 exe 名匹配（`Expect=` / `Block=`），被两个真实案例否决：

- 文件名带空格时，Lua 的 `[%w_%-%.]+%.exe` 只提取出最后一个词，写入的名字与真实进程不符；
- 双进程游戏（小启动器 + `*-Win64-Shipping.exe` 主进程）：启动器一退出，「进程消失即解锁」就放行了，而游戏其实还在跑。

---

## 6. 全屏判定与分辨率

```cpp
HMONITOR hm = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
GetMonitorInfoW(hm, &mi);                 // 失败才回退 SM_CXSCREEN/SM_CYSCREEN
double cw = win_w / monitor_w, ch = win_h / monitor_h;
bool fullscreen = (cw >= Coverage && ch >= Coverage);
```

- 取 **Z 序第一个「实质窗口」**（非系统类、非 tool window、≥320×240、面积 ≥ 显示器 10%）。真实游戏在 `Launch` 之后会抢到前台，250 ms 后它就是这个窗口。
- 曾经放宽到「前 6 个实质窗口任一达标」，结果桌面上常驻的最大化浏览器（2062×1126 / 2048×1152，`ch=0.977`）在游戏窗口销毁后仍满足 0.95 之类的阈值，任务栏永不还原。已回退到单窗口判据。
- 所有阈值都是**相对比例**，源码里没有硬编码分辨率；`GetWindowRect` 与 `GetMonitorInfo` 处于同一 DPI 上下文，缩放变化不影响比值。

| 场景 | cw / ch | `Coverage=0.98` |
| --- | --- | --- |
| 1920×1080 串流 + 全屏 | 1.000 / 1.000 | 让位 |
| 3840×2160 全屏 | 1.000 / 1.000 | 让位 |
| 1080p 无边框 1920×1040 | 1.000 / 0.963 | 不让位 |
| 2048×1152 桌面 + 1920×1080 窗口 | 0.938 / 0.938 | 不让位 |

降低 `Coverage` 到 0.93 能覆盖后两行，但也会把最大化浏览器算成全屏，需要权衡。

---

## 7. 已知边角情况

- **explorer 重建任务栏**：串流连接/分辨率热切换时，`Shell_TrayWnd` 句柄可能变化。让位期间保存的是旧 HWND，`g_trayActive` 仍为 true，不会重新采集，新任务栏会保持 topmost。发 `Stop` 或重启 Rainmeter 即可；自动重采集（比较主 tray HWND）尚未实现。
- **强杀 Rainmeter / 串流软件接管**：没有 `Finalize` 机会，或外部工具直接改了任务栏的 topmost 位，任务栏可能停在让位状态。插件只在 `g_trayActive` 为真时还原「自己让位过」的窗口，不会主动把外部改动拉回。用 `Stop` / `Restore` 或重启 explorer 恢复。`tests/ghf_layer_e2e.py` 会在开始时归一化 baseline。
- **两个插件实例并存**：跑独立 e2e 测试时如果 Rainmeter 也加载了同一个 DLL，两个实例会同时压/还任务栏，让位采样出现波动（但不影响门控结论）。测试脚本用 `SetWindowPos(HWND_TOPMOST)` 做真实归一化，而不是只改 `GWL_EXSTYLE` 样式位——后者会留下与 Z 序不一致的假 topmost 标志。
- **平台进程承载的游戏**：极少见，会话门控可能抓不到，但全屏前台兜底仍会生效。

---

## 8. 验证记录（v2）

| 测试 | 结果 |
| --- | --- |
| 通用门控：任意名字窗口进程出现 → `game=1`；进程终止 → `game=0` | PASS（0.2 s 内） |
| 任务栏让位 / 精确还原 | PASS（0.2 s 内让位、0.2 s 内还原，最终 `topmost=True`） |
| 6 轮 `Launch → 全屏 → 退出 → 刷新`，269 次 `Update()`（本仓库 `scripts/verify.sh`） | PASS（6/6 让位、6/6 还原，峰值 14.95 ms） |
| 25 次 `Initialize/Finalize` 刷新压力 | PASS（无死锁） |
| 导出表 | 6 个导出齐全；导入表无 `CreateTimerQueueTimer` / `SHQueryUserNotificationState` |
| 皮肤文件保真 | CRLF / 无 BOM 未变，`GameHUBFix.ini` 为紧凑 CRLF |
