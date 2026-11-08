# GameHUBFix

Rainmeter 5.0（64 位）+ GameHUB 2 的插件。修三件事：

- 全屏游戏里按手柄 Start 不会误唤出 GameHUB 2；
- 退出游戏后 Start 能立刻再用；
- 手柄启动游戏时，桌面皮肤 / BitDock / 任务栏不会盖在游戏前面。

## 安装

1. 退出 Rainmeter，把 `dist/GameHUBFix.dll` 和 `config/GameHUBFix.ini` 一起放到 `%APPDATA%\Rainmeter\Plugins\`（两个文件必须同目录）。
2. 按下面接线，再启动 Rainmeter，把 **`GameHUB 2\Button` 设为常驻（`Active=1`）**。Button 是 GameHUB 2 自带的桌面图标，本来就适合常驻；不需要装任何其它皮肤。

## 使用

把这段加到 `Skins\GameHUB 2\Button\Button.ini`（里面若已有 `[GamepadActivation]`，改成下面这样即可，不要重复添加；`[HOTKEY]` 段保持原样）：

```ini
[Rainmeter]
Update=100

[MeasureGameGuard]
Measure=Plugin
Plugin=GameHUBFix
UpdateDivider=1

[GamepadActivation]
Measure=Plugin
Plugin=XInput
Device=0
Channel=Start
IfCondition=(GamepadActivation = 1) && (MeasureGameGuard = 0)
IfTrueAction=[!ActivateConfig "#ROOTCONFIG#" "GameHUB.ini"]
DynamicVariables=1

[GamepadActivation2]
Measure=Plugin
Plugin=XInput
Device=1
Channel=Start
IfCondition=(GamepadActivation2 = 1) && (MeasureGameGuard = 0)
IfTrueAction=[!ActivateConfig "#ROOTCONFIG#" "GameHUB.ini"]
DynamicVariables=1
```

别给 Button 加 `Group=GameHUB2`（GameHUB 退出时会关掉整个组，Start 就唤不出了）。

再补两处：

`Skins\GameHUB 2\@Resources\Include\input.inc` 末尾追加：

```ini
[GameHUBFix]
Measure=Plugin
Plugin=GameHUBFix
UpdateDivider=-1
```

`Skins\GameHUB 2\@Resources\Script\horizontal.lua` 和 `vertical.lua`，启动游戏命令前加一行：

```lua
SKIN:Bang('!CommandMeasure "GameHUBFix" "Launch"')
```

不想让 Button 常驻的话，把上面那几个 measure 放进任意常驻皮肤也行。

## 配置

`%APPDATA%\Rainmeter\Plugins\GameHUBFix.ini`：

```ini
[GameHUBFix]
Enabled=1
TickMs=250
SettleMs=10000
Coverage=0.98
SessionMs=90000
```

默认值直接用，不用改。改完刷新承载插件的那个皮肤即可。
