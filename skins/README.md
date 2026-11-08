# Rainmeter 接线示例

本目录只含接入 `GameHUBFix.dll` 所需的**最小片段**，不是完整皮肤，也不包含 GameHUB 2 / Another dock 的任何素材。

| 文件 | 放到哪里 | 作用 |
| --- | --- | --- |
| `PadListener.ini` | `Skins\Another dock\PadListener\PadListener.ini` | **完整文件**，常驻监听 Start 键（新文件，可直接复制） |
| `Dock.ini.snippet` | `Skins\Another dock\Dock.ini` 的 `[Rainmeter]` 段 | 刷新 dock 时自动激活监听皮肤 |
| `input.inc.example` | `Skins\GameHUB 2\@Resources\Include\input.inc` 末尾 | 声明供 Lua 调用的 `[GameHUBFix]` measure |
| `launch.lua.snippet` | `Skins\GameHUB 2\@Resources\Script\horizontal.lua` 与 `vertical.lua` 的 `interact()` | 启动外部游戏前发 `Launch` bang |

## 顺序

1. 复制 `PadListener.ini`（这是新文件，不会覆盖任何东西）。
2. 编辑 `Dock.ini`：把 snippet 里的 `!ActivateConfig` 追加到**现有** `OnRefreshAction` 的末尾，保留原来的样式初始化动作；然后删除旧的 `MeasureIsFullScreen` + `!DisableMeasure "GamepadActivation"` 门控段。
3. 编辑 `input.inc`：追加 `[GameHUBFix]` 段。
4. 编辑两个 Lua：在 `interact()` 里、真正的启动命令派发之前插入那一行。
5. 启动/刷新 Rainmeter，确认 `[Another dock\PadListener] Active=1`。

## 两点容易踩的坑

- **`GameHUB 2\Button\Button.ini` 不是接线点。** 这套皮肤默认 `Active=0`，里面即使存在 `[MeasureIsFullScreen]`、`[MeasureGameGuard]`、`[GamepadActivation]` 也全是死代码，从未被加载。排查 Start 问题时请只看 `PadListener.ini`。
- **`IsFullScreen.dll` 已弃用。** 它仍可能留在 `Plugins` 目录里（不加载不占资源），但没有任何生效的皮肤需要引用它。`HotKey.dll`（键盘快捷键）与 `XInput.dll`（手柄轮询）**仍然需要**。

## 编码与换行

Rainmeter 皮肤文件对编码敏感。这套皮肤原文件是 **UTF-8 无 BOM + CRLF**；编辑时请保持，否则可能出现变量解析异常。Lua 与 ini 都不要改成 UTF-16 或 LF。
