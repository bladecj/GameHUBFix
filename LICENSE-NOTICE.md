# 许可与第三方来源

## 本仓库文件

| 文件 | 来源 | 许可 |
| --- | --- | --- |
| `src/GameHUBFix.cpp` | 本项目作者编写 | **未指定开源许可证**（默认保留所有权利） |
| `tests/*`、`scripts/*`、`skins/*`、`docs/*`、`README.md` | 本项目作者编写 | 同上 |
| `src/RainmeterAPI.h` | Rainmeter 官方插件 SDK | **GPL-2.0-or-later** |
| `dist/GameHUBFix.dll` | 由 `src/` 编译产生 | 同源码；另含静态链接的 MinGW-w64 运行时（其自身许可见 MinGW-w64 项目） |

Rainmeter 是 [rainmeter/rainmeter](https://github.com/rainmeter/rainmeter) 项目，其插件 API 头文件版权为
`Copyright (C) 2011 Kimmo Pekkola, Birunthan Mohanathas`，按 GNU GPL v2 或更高版本授权。本仓库原样附带该头文件，
仅为让 `src/GameHUBFix.cpp` 中的 `#include "RainmeterAPI.h"`（`PLUGIN_EXPORT` 宏）能够独立编译。
插件本身**不调用**任何 `Rm*` 函数，也不链接 Rainmeter 的库。

## 关于是否添加 LICENSE

本仓库**刻意不附带 `LICENSE` 文件**，以免替作者做出授权决定。若你希望他人可以合法使用、修改或分发这份代码：

1. 在项目根目录添加一个 `LICENSE`（MIT / Apache-2.0 / GPL-2.0-or-later 等），并写明版权年份与作者；
2. 注意 `src/RainmeterAPI.h` 是 GPL-2.0-or-later：若整体以更宽松的许可发布，建议把该头文件改为「从 Rainmeter 官方 SDK 获取」的外部依赖，或确认所选许可与 GPL 兼容；
3. 同步更新 `README.md` 的「许可与来源」一节。

## 第三方作品

- **Rainmeter**、**GameHUB 2**、**Another dock**、**BitDock** 均为各自作者的作品，本仓库不包含它们的完整代码、图片或音频素材。
- `skins/` 只包含为了接入本插件所必需的最小 ini / lua 片段，用于说明接线位置。
- 插件运行时会枚举并临时调整 explorer 的任务栏窗口与桌面 overlay 的 Z 序，但不修改任何第三方程序的文件或设置。
