# 更新记录

## v2（当前版本，单线程稳定版）

- **彻底移除后台线程**：删除 timer queue、`CRITICAL_SECTION`、DLL 自引用（`LoadLibrary` 保活）与 `SHQueryUserNotificationState`（COM 调用）。全部工作由 `Update()` 在 Rainmeter 主线程驱动。
- **所有 `SetWindowPos` 加 `SWP_ASYNCWINDOWPOS`**：主线程永不等待 explorer 处理跨进程 Z 序变更。这修掉了「关闭游戏瞬间 Rainmeter 整体卡死、只能任务管理器强杀」的问题。
- **门控完全通用化**：不再匹配任何游戏 exe 名。改为 `Launch` 时拍进程基线，之后新出现且拥有可见窗口的非平台进程构成一次「启动会话」，任一存活即拦截 Start，全部消失立即放行。带空格/中文路径、`启动器 + *-Win64-Shipping.exe` 双进程、未来新游戏都无需配置。
- **修正 `visiblePids` 枚举**：`ScanCB` 不再在遇到第一个实质窗口时停止，否则被别的窗口压住的游戏进程会漏出会话门控。
- **修正 fallback latch 释放**：进程仍在但不再拥有可见窗口时也会解锁，避免常驻宿主进程让 Start 永久失效。
- **全屏判定保持「Z 序第一个实质窗口」**：中途试过放宽到前 6 个窗口，会让桌面上常驻的最大化浏览器把任务栏永远压在让位状态，已回退。
- 配置精简为 5 个通用参数：`Enabled / TickMs / SettleMs / Coverage / SessionMs`。
- 验证：通用门控、任务栏让位/还原、6 轮「启动→全屏→退出→刷新」+ 283 次 `Update()`（峰值约 16.6 ms）全部 PASS。

## v1（已废弃，归档为 broken / deadlock 版本）

- 把早期三个插件（`GameGuard.dll` 门控、`ForegroundFix.dll` 皮肤 Z 序、`TrayFix.dll` 任务栏 Z 序）合并为单个 `GameHUBFix.dll`，共用一个 timer 与一份配置。
- 门控按「游戏 exe 名 + `Expect`/`Arm`/`Block` 配置」实现，被证明脆弱：文件名带空格时 Lua 正则提取出错误的名字，双进程游戏（启动器退出后主进程接管）会让锁提前释放。
- **两处死锁**：
  1. `Finalize` 持有临界区时调用 `DeleteTimerQueueTimer(..., INVALID_HANDLE_VALUE)` 等待 timer 回调，而回调正等着同一把锁 → 皮肤刷新时互等。
  2. timer 回调持锁调用 `BeginStepAside / RestoreTaskbar / PushBehind`，这些是对 explorer 任务栏窗口的**同步**跨进程 `SetWindowPos`；游戏退出时 shell 正在重建任务栏，回调卡住不放锁，Rainmeter 主线程每 100 ms 的 `Update()` 全部阻塞 → 界面无响应。
- 第一处修过（timer 改进程级常驻），第二处未覆盖，最终触发用户实测的卡死，因此整体重构为 v2。

## 前身：三个独立插件（已退役，不在本仓库）

| 插件 | 职责 | 退役原因 |
| --- | --- | --- |
| `GameGuard.dll` | Start 门控 | 迭代中升级到 v3（按进程判定），最终并入 GameHUBFix |
| `ForegroundFix.dll` | ALT 脉冲 + 压皮肤/BitDock | 与 TrayFix 扫描同一份 Z 序，合并 |
| `TrayFix.dll` | 临时摘掉任务栏 topmost | 同上 |

另：原版 `IsFullScreen.dll` / `HotKey.dll` / `XInput.dll` 的 DLL 文件从未修改。`HotKey.dll` 继续提供键盘快捷键，`XInput.dll` 继续轮询手柄；`IsFullScreen.dll` 已不再被任何生效的皮肤引用。
