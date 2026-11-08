# GameHUBFix

一个给 **Rainmeter 5.0（64 位）** 用的插件 DLL，解决用手柄 **Start 键唤出 GameHUB 2** 时的三类问题：游戏中被误唤出、退出游戏后唤不出、以及全屏游戏被桌面皮肤和任务栏盖住。

> **English summary** — A single x64 Rainmeter 5.0 plugin that (1) blocks the gamepad Start key while a game is running, using a generic *launch-session* heuristic with **no game names**, (2) fixes the foreground/Z-order hand-off when a game is launched from a gamepad, and (3) temporarily steps the Windows taskbar out of the topmost group while a full-screen application is frontmost. One DLL, one INI, one `Update()`-driven loop, no background threads.

---

## 目录

- [解决的问题](#解决的问题)
- [设计要点](#设计要点)
- [目录结构](#目录结构)
- [一、安装（使用预编译 DLL）](#一安装使用预编译-dll)
- [二、Rainmeter 接线](#二rainmeter-接线)
- [三、配置项与日常使用](#三配置项与日常使用)
- [四、Bang 命令](#四bang-命令)
- [五、诊断与故障排查](#五诊断与故障排查)
- [六、编译方法](#六编译方法)
- [七、测试](#七测试)
- [八、已知限制与串流场景](#八已知限制与串流场景)
- [九、版本历史](#九版本历史)
- [十、许可与来源](#十许可与来源)

---

## 解决的问题

| 症状 | 原来的原因 | 本插件的做法 |
| --- | --- | --- |
| 全屏游戏里按 Start，GameHUB 2 还是弹出来（Start 本该是游戏菜单） | 旧门控用 `IsFullScreen.dll` 判断前台窗口：`EqualRect` 精确相等，独占全屏常返回空字符串，反而命中"桌面"白名单 | 用「启动会话中出现的新进程」判定，与窗口几何、焦点无关 |
| 退出游戏后 Start 失效，必须点一下桌面、甚至等 10 秒 / 开一次 Rainmeter 界面 | `Dock.ini` 里的 `!DisableMeasure` 把 XInput measure 关掉后不会自动恢复，且 `UpdateDivider=10` 在 `Update=1000` 下是 **10 秒** 才轮询一次 | 监听 measure 永远不被禁用；门控是数值 `0/1`，进程退出 0.1 秒内放行 |
| 手柄启动游戏时，Rainmeter 皮肤、BitDock、Windows 任务栏跑到游戏前面；鼠标点击启动则正常 | XInput 的 `XInputGetState()` 是轮询，不产生 Windows 输入事件，前台/Z 序交接没发生；任务栏是 `WS_EX_TOPMOST` 组，压低皮肤管不到它 | `Launch` 时补一次前台激活；全屏期间把桌面 overlay 压底、把任务栏条带临时摘掉 topmost，退出后精确还原 |

**为什么合并成一个插件**：项目早期按问题逐个新建了 `GameGuard.dll`（门控）、`ForegroundFix.dll`（皮肤 Z 序）、`TrayFix.dll`（任务栏 Z 序）。Rainmeter 运行时 DLL 文件被锁、不能原地覆盖，换个新名字最省事，于是越堆越多。实际上后两者扫描的是同一份 Z 序。现在三者合并，共用一个 `Update()`、一次 Z 序扫描、一份配置。

---

## 设计要点

- **不记游戏名。** 门控判据是「按下 Launch 之后新出现、且拥有可见窗口的非平台进程」。带空格/中文路径、启动器 + `*-Win64-Shipping.exe` 双进程、以后任何新游戏都不用配置。代码里的 `kPlatform[]` 不是游戏名单，只是排除 Steam/Epic/浏览器/系统后台等开机就在的辅助进程。
- **不依赖前台窗口判断游戏。** 旧 `IsFullScreen` 方案反复失效的根源就是它看前台窗口与桌面矩形；本插件看进程存活与窗口可见性。
- **单线程、无锁。** v1 曾用 timer queue + `CRITICAL_SECTION`，回调持锁对 explorer 做同步 `SetWindowPos`，游戏退出时 shell 忙 → 回调不返回 → Rainmeter 主线程的 `Update()` 永久等锁，整个界面卡死。v2 删掉了 timer、临界区和 DLL 自引用，全部工作由 `Update()` 在 Rainmeter 主线程驱动，**所有 `SetWindowPos` 都带 `SWP_ASYNCWINDOWPOS`**。
- **分辨率无关。** 全屏判定用 `MonitorFromWindow` + `GetMonitorInfoW` 动态取当前显示器尺寸，比较的是**比例**（窗口宽/显示器宽、窗口高/显示器高），源码里没有任何硬编码分辨率。详见[串流场景](#八已知限制与串流场景)。
- **纯 Win32。** 不调用任何 `Rm*` API、不写皮肤设置、不依赖 COM，插件之间无耦合。`GameHUBFix.ini` 由插件自己用 `GetPrivateProfileStringW` 读取。

三个功能对应到代码：

| 功能 | 入口 | 机制 |
| --- | --- | --- |
| Start 门控 | `Update()` 返回 `0`/`1` | 启动会话快照 + 全屏前台窗口兜底；诊断串由 `GetString()` 输出 |
| 前台 / 皮肤 Z 序 | `Launch` / `Settle` bang | ALT 输入脉冲 + `AttachThreadInput` + `AllowSetForegroundWindow(ASFW_ANY)`；把 `RainmeterMeterWindow` / BitDock 压到 `HWND_BOTTOM` |
| 任务栏 Z 序 | 自动 | 全屏时把 explorer 拥有、与任务栏矩形重叠的 topmost 窗口摘 topmost 压底；记录 `GWL_EXSTYLE` 与 `GW_HWNDPREV`，窗口消失后精确还原 |

---

## 目录结构

```
GameHUBFix-master/
├── README.md                    本文件
├── CHANGELOG.md                 版本历史
├── LICENSE-NOTICE.md            许可与第三方来源说明
├── .gitignore
├── src/
│   ├── GameHUBFix.cpp            插件全部源码（单文件）
│   └── RainmeterAPI.h           Rainmeter 官方插件 API 头（GPL-2.0-or-later）
├── config/
│   └── GameHUBFix.ini           配置示例（复制到 Rainmeter 插件目录）
├── dist/
│   └── GameHUBFix.dll           预编译 x64 DLL（由本仓库 src/ 编译生成）
├── skins/                       Rainmeter 接线示例（不是完整皮肤）
│   ├── README.md
│   ├── PadListener.ini          常驻监听皮肤（完整文件）
│   ├── input.inc.example        GameHUB 2 的 measure 段
│   ├── Dock.ini.snippet         Another dock 的 OnRefreshAction
│   └── launch.lua.snippet       GameHUB 2 启动游戏时的一行钩子
├── scripts/
│   ├── build.sh                 MinGW-w64 编译脚本
│   ├── build.bat                Windows 下等价脚本
│   ├── deploy.bat               部署到 %APPDATA%\Rainmeter\Plugins
│   └── verify.sh                编译后跑全部独立测试
├── tests/                       独立进程测试（不需要 Rainmeter 运行）
│   ├── fake_game.cpp            任意名字的假游戏进程
│   ├── ghf_probe.cpp            通用门控 ABI 探针
│   ├── ghf_layer_e2e.py         任务栏让位/还原端到端
│   ├── ghf_v2_stress.py         启动→退出循环 + 刷新压力 + Update 延迟
│   └── ghf_scan_diag.py         按插件视角打印 Z 序与全屏判定
└── docs/
    └── TECHNICAL.md             深入原理、死锁复盘、串流分辨率推演
```

---

## 一、安装（使用预编译 DLL）

**前提**：Rainmeter 5.0 或更新版本，**64 位**。本插件不支持 32 位 Rainmeter / 4.x。

1. **完全退出 Rainmeter**（托盘图标 → 退出）。正在运行的 Rainmeter 会锁住 `Plugins` 目录里的 DLL，无法覆盖。
2. 把 `dist/GameHUBFix.dll` 和 `config/GameHUBFix.ini` 一起复制到：

   ```
   %APPDATA%\Rainmeter\Plugins\
   ```

   两个文件必须放在同一目录——插件按自身 DLL 所在路径去找 ini。
3. 把 `skins/PadListener.ini` 复制到：

   ```
   %USERPROFILE%\Documents\Rainmeter\Skins\Another dock\PadListener\PadListener.ini
   ```

4. 按[下一节](#二rainmeter-接线)完成 3 处接线。
5. 启动 Rainmeter。确认 `Another dock\PadListener` 处于 **Active=1**（可在 Rainmeter 管理界面查看）。
6. 确认插件已加载：任务管理器 → Rainmeter 进程 → 模块中应能看到 `GameHUBFix.dll`；或在日志里看 `PadListener Start:` 行。

> **已有一套旧环境？** 如果之前装过 `GameGuard.dll` / `ForegroundFix.dll` / `TrayFix.dll`，请把它们的 `[Measure...]` 段从皮肤里删掉、把 DLL 移出 `Plugins` 目录，避免重复扫描和旧配置干扰。本仓库不包含这三个退役插件。

---

## 二、Rainmeter 接线

一共 **4 处**。`skins/` 里有可直接复制的片段。

### 1. 常驻监听皮肤 `Another dock\PadListener\PadListener.ini`

完整文件见 `skins/PadListener.ini`。核心部分：

```ini
[Rainmeter]
Update=100
OnRefreshAction=[!ClickThrough 1][!ZPos -2][!KeepOnScreen 0][!Draggable 0]

[MeasureGameGuard]          ; 历史命名，实际指向 GameHUBFix
Measure=Plugin
Plugin=GameHUBFix
UpdateDivider=1

[GamepadActivation]
Measure=Plugin
Plugin=XInput
Device=0
Channel=Start
IfCondition=(GamepadActivation = 1) && (MeasureGameGuard = 0)
IfTrueAction=[!ActivateConfig "GameHUB 2" "GameHUB.ini"]
DynamicVariables=1
; 第二个手柄把 Device=1、measure 名改为 GamepadActivation2 即可
```

`Update=100` 是 10 Hz 轮询，短按不会漏。**不要把 `Group=GameHUB2` 加进来**——GameHUB 退出时会 `!DeactivateConfigGroup`，那会把监听一起关掉。

### 2. `Another dock\Dock.ini` — 自动激活监听

在 `[Rainmeter]` 段的 `OnRefreshAction` 末尾追加：

```ini
[!ActivateConfig "Another dock\PadListener" "PadListener.ini"]
```

原来的样式初始化动作要保留，完整写法见 `skins/Dock.ini.snippet`。同时**删除**旧的 `MeasureIsFullScreen` + `!DisableMeasure "GamepadActivation"` 门控（它会让 XInput measure 永远停在被禁用状态）。

### 3. `GameHUB 2\@Resources\Include\input.inc` — 声明 measure

```ini
[GameHUBFix]
Measure=Plugin
Plugin=GameHUBFix
UpdateDivider=-1
```

`UpdateDivider=-1` 表示这个 measure 自己不轮询；真正的轮询由常驻的 PadListener 承担（状态是进程全局的，两个皮肤共享同一个 DLL 实例状态）。

### 4. `horizontal.lua` / `vertical.lua` — 启动游戏时通知插件

在 `interact()` 里、真正执行启动命令之前加一行：

```lua
if string.sub(command,1,1) ~= '[' then SKIN:Bang('!CommandMeasure "GameHUBFix" "Launch"') end
```

只对外部程序启动（命令不以 `[` 开头）触发，避免把 GameHUB 内部 bang 当成一次游戏启动。见 `skins/launch.lua.snippet`。

> ⚠️ **`GameHUB 2\Button\Button.ini` 不是接线点。** 那套皮肤默认 `Active=0`，里面即使写了 `[MeasureGameGuard]` 也是死代码。真正生效的只有上面的 PadListener。排查时别看 Button。
>
> **`IsFullScreen.dll` 已不再用于 Start 门控。** DLL 文件可以留着（不加载就不占资源），但没有任何皮肤需要引用它。

---

## 三、配置项与日常使用

**日常零操作**：启动游戏、退出游戏、按 Start 全部自动。配置文件：`%APPDATA%\Rainmeter\Plugins\GameHUBFix.ini`

```ini
[GameHUBFix]
Enabled=1
TickMs=250
SettleMs=10000
Coverage=0.98
SessionMs=90000
```

| 键 | 默认 | 合法范围 | 说明 |
| --- | --- | --- | --- |
| `Enabled` | `1` | `0` / `1` | 总开关。设为 `0` 会先还原任务栏再停止层级工作；门控也一并关闭 |
| `TickMs` | `250` | 100–2000 | 层级工作（Z 序扫描、压底、任务栏让位）的最小间隔，毫秒。**门控每次 `Update()` 都算**，不受此值限制 |
| `SettleMs` | `10000` | 1000–120000 | `Launch` 之后持续把皮肤/BitDock 压到底部的时长。启动特别慢的游戏可改 `20000`–`30000` |
| `Coverage` | `0.98` | 0.50–1.0 | 判定「全屏」的宽/高覆盖率阈值。见[串流场景](#八已知限制与串流场景) |
| `SessionMs` | `90000` | 5000–600000 | `Launch` 之后等待「新游戏进程出现」的时间窗。用超慢启动器时可加大 |

改完刷新 `Another dock\PadListener` 即生效（插件在 `Reload` 里重新读配置），**刷新不会清掉正在进行的游戏会话**，所以不会在玩游戏时误放行 Start。

> ini 由插件自己读取、从不交给 Rainmeter 的 `!WriteKeyValue` 写入，理论上可以加注释。但保持「只有 section + 键、无注释无空行」的紧凑格式最稳妥——历史上 Rainmeter 的 `!WriteKeyValue` 曾把注释粘连到相邻键上，导致整段配置失效。如果以后让皮肤写这个文件，请务必保持紧凑。

---

## 四、Bang 命令

用法：`!CommandMeasure "GameHUBFix" "<命令>"`（需要在加载了该 measure 的皮肤上下文里执行）。

| 命令 | 作用 |
| --- | --- |
| `Launch` | 启动游戏时调用：输入脉冲 + 前台激活 + 开启 Settle + 拍进程基线。**由 Lua 自动调用，一般不用手动发** |
| `Pulse` | 只做前台脉冲，不改会话状态 |
| `Arm` | 只拍一次进程基线、开启一次启动会话 |
| `Settle` | 重新采集桌面 overlay 并重新启动一轮压底 |
| `Stop` | 结束压底、还原任务栏、清空会话（任务栏状态异常时用它重置） |
| `Reset` / `Unblock` | 强制解除 Start 拦截，但保留层级工作 |
| `Restore` | 只还原任务栏 |
| `Disable` / `Enable` | 临时关 / 开层级工作并热更新状态（不重新读文件） |
| `Reload` | 重新读取 `GameHUBFix.ini` |

---

## 五、诊断与故障排查

按 Start 时 PadListener 会往日志写一行：

```
PadListener Start: GameHUBFix: game=0 session=0 pids=0 fallback=0 lock=0 fg=(desktop) tray=0 settle=0 cycles=12345
```

| 字段 | 含义 |
| --- | --- |
| `game` | 门控输出：`1` = 判定在游戏中，Start 被拦；`0` = 放行 |
| `session` / `pids` | 启动会话是否活跃 / 当前锁存的游戏进程数 |
| `fallback` / `lock` | 全屏前台窗口兜底是否生效 / 锁住的 PID |
| `fg` | 前台窗口所属进程名（`(desktop)` 表示桌面） |
| `tray` | `1` = 任务栏已让位，`0` = 未让位 |
| `settle` | `1` = 正在压底皮肤/BitDock |
| `cycles` | `Update()` 累计次数。数值不再增长说明皮肤没在更新 |

也可以把 `[MeasureGameGuard:]`（`GetString`）显示在一个 String meter 上实时观察。

| 现象 | 先看哪个字段 | 处理 |
| --- | --- | --- |
| Start 完全没反应 | — | 确认 `[Another dock\PadListener] Active=1`、`XInput.dll` 与 `GameHUBFix.dll` 已加载、手柄 `Device` 号正确 |
| 游戏里仍能唤出 | `game=0 session=0` | Lua 的 `Launch` 没被调用（检查 `horizontal.lua`/`vertical.lua` 那一行），或该游戏不是从 GameHUB 启动且未触发全屏兜底 |
| 退出游戏后仍唤不出 | `game=1 pids>0` | 有残留进程还活着；发 `!CommandMeasure "GameHUBFix" "Reset"` 可立即放行，再查是哪个进程没退出 |
| 任务栏仍盖在游戏上 | `tray=0` | 游戏窗口没有铺满显示器 ≥98%；调低 `Coverage`，或确认游戏窗口确实抢到了前台 |
| 皮肤/BitDock 盖在游戏上 | `settle=0` | 游戏启动太慢，超过了 `SettleMs`；调大到 `20000`–`30000` |
| Rainmeter 整体卡死 | — | 确认用的是 **v2** DLL；v1 的 timer + 临界区 + 同步 `SetWindowPos` 会死锁（见 `docs/TECHNICAL.md`） |
| 桌面状态下任务栏没恢复 | `tray=1` | 发 `Restore` 或 `Stop`；插件卸载时也会自动还原 |

---

## 六、编译方法

### 环境要求

- **MinGW-w64 的 x86_64 工具链**（`x86_64-w64-mingw32-g++`）。Windows 上可用 MSYS2 (`mingw-w64-x86_64-gcc`)、mingw-builds、或自带该编译器的发行版；Linux/macOS 上可用交叉编译包 `gcc-mingw-w64-x86-64`。
- 只需要系统库 `psapi`（MinGW 自带导入库），**不需要 Rainmeter 源码或 SDK 的其他文件**：本插件不调用任何 `Rm*` 函数，`RainmeterAPI.h` 仅用于 `PLUGIN_EXPORT` 宏。
- 输出必须是 **64 位 DLL**，与 64 位 Rainmeter 匹配。

### 一条命令

在项目根目录执行：

```bash
x86_64-w64-mingw32-g++ \
  -O2 -std=c++11 -shared \
  -static-libgcc -static-libstdc++ \
  -o GameHUBFix.dll \
  src/GameHUBFix.cpp \
  -lpsapi
```

或直接用脚本：

```bash
bash scripts/build.sh          # 产出 GameHUBFix.dll，并打印导出表
```

Windows 命令行：

```bat
scripts\build.bat
```

参数说明：

| 参数 | 原因 |
| --- | --- |
| `-shared` | 产出 DLL |
| `-static-libgcc -static-libstdc++` | 静态链接运行时，目标机器不必装 MinGW 运行库 |
| `-lpsapi` | `CreateToolhelp32Snapshot` / 进程枚举相关（`tlhelp32`、`psapi`） |
| `-O2 -std=c++11` | 源码用到 `std::set`、`GetTickCount64`、`<algorithm>` 等 |
| 无需 `-luser32` 等 | MinGW 的默认导入库已包含 `user32`/`kernel32`/`shell32` |

### 校验产物

```bash
objdump -p GameHUBFix.dll | sed -n '/Ordinal\/Name Pointer/,/^$/p'
```

必须恰好导出 6 个符号：`Initialize`、`Reload`、`Update`、`GetString`、`ExecuteBang`、`Finalize`。同时确认导入表里没有 `CreateTimerQueueTimer`、`SHQueryUserNotificationState`——出现它们说明拿错了旧版本。

产物大小约 619 KB。PE 头的时间戳和链接期随机数会让**每次编译的 SHA-256 不同**，这是正常的；判断版本请用导出表和 `tests/` 的行为测试，不要用哈希。

### 部署

```bat
scripts\deploy.bat
```

脚本会把 `GameHUBFix.dll` 与 `config\GameHUBFix.ini` 复制到 `%APPDATA%\Rainmeter\Plugins\`。**执行前必须先退出 Rainmeter**，否则 DLL 被占用无法覆盖；也可以先把旧 DLL 重命名（Windows 允许重命名已加载的 DLL），复制新文件后再重启 Rainmeter。

### MSVC 说明

源码只依赖 Win32 API 与标准 C++，`cl /LD /O2 /EHsc src\GameHUBFix.cpp /link psapi.lib` 理论上可行，但本项目**只验证过 MinGW-w64 产物**，`dist/` 中的 DLL 也是用它编译的。若换 MSVC，请自行跑一遍 `tests/` 再投入使用。

### 常见编译问题

| 报错 | 原因 |
| --- | --- |
| `RainmeterAPI.h: No such file` | 头文件与 `GameHUBFix.cpp` 必须同目录（`src/`）；`#include "..."` 会先在源文件目录查找 |
| `undefined reference to ...` | 漏了 `-lpsapi`，或用了 32 位工具链（`i686-w64-mingw32-g++`） |
| 编译成功但 Rainmeter 报「不是有效插件」 | 目标平台不是 x64，或 6 个导出符号缺失 |
| 覆盖 DLL 时提示文件被占用 | Rainmeter 还在运行，先退出 |

---

## 七、测试

`tests/` 全部是**独立进程**测试，直接 `LoadLibrary` 加载 DLL 并模拟 Rainmeter 调用 `Update()`，不需要 Rainmeter 运行，也不会留下常驻改动（脚本均在 `finally` 里还原任务栏）。屏幕尺寸在运行时读取，1080p / 4K / 多显示器都能跑。

> 传入 DLL 路径时请使用**绝对路径**；`ctypes.CDLL` 在部分 Windows 环境下不解析相对路径。
>
> **建议先退出 Rainmeter 再跑测试**：如果 Rainmeter 里也加载了同一个 DLL，两个实例会同时调整任务栏 Z 序，让位采样会有波动。脚本会归一化并始终还原任务栏，但独占运行的结果最干净。
>
> 若开始时任务栏已处于非 topmost（上一次串流/测试遗留），`ghf_layer_e2e.py` 会先把它恢复正常再测，避免把错误状态当成 baseline。

```bash
# 1) 编译假游戏与 ABI 探针
x86_64-w64-mingw32-g++ -O2 -municode -o SessionGameTest.exe tests/fake_game.cpp
x86_64-w64-mingw32-g++ -O2 -municode -o ghf_probe.exe    tests/ghf_probe.cpp

# 2) 通用门控：任意名字的窗口进程出现即拦截，进程终止即放行
./ghf_probe.exe "C:\full\path\to\GameHUBFix.dll" "SessionGameTest.exe"
#    期望 RESULT generic-block=1 auto-release=0 -> PASS

# 3) 任务栏让位 / 精确还原（全透明全屏窗口，视觉无感）
python tests/ghf_layer_e2e.py "C:\full\path\to\GameHUBFix.dll"
#    期望 0.2s 内让位、关闭后 0.2s 内还原、最终 topmost=True -> PASS

# 4) 6 轮 启动→全屏→退出→刷新，并统计每次 Update() 耗时
python tests/ghf_v2_stress.py "C:\full\path\to\GameHUBFix.dll"
#    期望 6/6 让位、6/6 还原、Update 峰值 < 50ms -> PASS

# 5) 按插件视角打印 Z 序与「首个实质窗口」的全屏判定
python tests/ghf_scan_diag.py "C:\full\path\to\GameHUBFix.dll" [launch]
```

`scripts/verify.sh` 会把 2–4 步串起来跑一遍（需要 Python 3 与 MinGW-w64）。

---

## 八、已知限制与串流场景

### 分辨率 / 串流

判据是比例：`cw = 窗口宽 / 显示器宽`、`ch = 窗口高 / 显示器高`，两者都 ≥ `Coverage` 才算全屏。显示器尺寸由 `MonitorFromWindow` + `GetMonitorInfoW` 动态取得（取不到时回退 `SM_CXSCREEN/SM_CYSCREEN`），因此虚拟显示器、多显示器、DPI 缩放、分辨率热切换都能自适应。

| 场景 | cw / ch | 结果 |
| --- | --- | --- |
| 1920×1080 串流 + 全屏游戏 | 1.000 / 1.000 | 正常让位 |
| 3840×2160 全屏 | 1.000 / 1.000 | 正常让位 |
| 1080p 无边框窗口 1920×1040 | 1.000 / 0.963 | **不让位** |
| 桌面仍是 2048×1152、游戏只有 1920×1080 窗口 | 0.938 / 0.938 | **不让位** |

**Start 门控与皮肤 Z 序完全不受分辨率影响**——前者只看进程与可见窗口，后者只做 Z 序压底。上表后两行只影响任务栏让位。

若串流时游戏是窗口化/无边框、或串流服务没有把桌面分辨率一起切过去，把 `Coverage` 临时调到 `0.93`–`0.95` 即可。⚠️ **副作用**：桌面上常驻的最大化浏览器（例如 2062×1126 在 2048×1152 上，`ch=0.977`）也会被判成全屏，导致任务栏长期让位。因此不建议把它当作常设值。

### 其它限制

- 只支持 64 位 Rainmeter 5.0+；32 位与 4.x 未测试。
- 串流或分辨率热切换时，若 explorer **重建**了 `Shell_TrayWnd`，让位期间保存的旧窗口句柄会失效，可能出现任务栏重新盖住游戏。发一次 `!CommandMeasure "GameHUBFix" "Stop"` 或重启 Rainmeter 即可恢复；自动重采集尚未实现。
- `kPlatform[]` 会排除常见平台/浏览器/系统进程。如果某个游戏本身由这些进程承载（极少见），会话门控可能抓不到它，此时全屏前台窗口兜底仍会生效。
- 插件在 `Finalize` 里会自动还原任务栏，但**强杀 Rainmeter 进程**没有 `Finalize` 机会；串流软件、远程桌面或强杀都可能把任务栏留在非 topmost。插件只会还原「自己让位过」的窗口，不会主动把外部改动拉回来。发现任务栏被最大化窗口盖住时，执行一次 `!CommandMeasure "GameHUBFix" "Stop"` / `Restore`，或重启 explorer。`tests/ghf_layer_e2e.py` 会自动做这个归一化。

---

## 九、版本历史

见 [CHANGELOG.md](CHANGELOG.md)。简要：

- **v1**（已废弃）：三个独立插件 → 合并为一个 `GameHUBFix.dll`，但使用 timer queue + 临界区，先后出现两处死锁（`Finalize` 与回调互等；回调持锁同步调用 explorer）。
- **v2（当前）**：单线程 `Update()` 驱动、所有窗口操作异步；门控改为完全通用的启动会话；修好 `visiblePids` 完整枚举与 fallback latch 释放。

---

## 十、许可与来源

详见 [LICENSE-NOTICE.md](LICENSE-NOTICE.md)。要点：

- `src/RainmeterAPI.h` 来自 Rainmeter 官方插件 SDK，版权归其作者，按 **GPL-2.0-or-later** 授权；本仓库仅原样附带以便编译。
- `src/GameHUBFix.cpp`、`tests/`、`scripts/` 与文档由本项目作者编写。**本仓库未附带开源许可证**，默认保留所有权利；若你希望允许他人使用/分发，请自行添加 `LICENSE`（如 MIT）并确认与上述头文件许可的兼容性。
- Rainmeter、GameHUB 2、Another dock、BitDock 均为各自作者的作品。`skins/` 只含必要的接线片段，不包含任何皮肤的完整代码或素材；插件本身与它们没有代码依赖。

---

## 反馈

遇到问题时请附上：Rainmeter 版本、日志里的 `PadListener Start: GameHUBFix: ...` 诊断串、`GameHUBFix.ini` 的内容，以及复现步骤（哪个游戏、全屏还是窗口化、是否经 GameHUB 启动、是否串流）。
