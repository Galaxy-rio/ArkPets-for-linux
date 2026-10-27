# ArkPets Linux

面向 Arch Linux 与 Hyprland 的桌宠程序，受 [Ark-Pets](https://github.com/isHarryh/Ark-Pets) 启发，使用 [Ark-Models](https://github.com/isHarryh/Ark-Models) 提供的模型索引。界面基于 Qt Quick 和 [material-components-qml](https://github.com/sudoevolve/material-components-qml)，角色通过 GTK4 与 Wayland layer-shell 显示。

## 功能

- 搜索并按需下载模型，支持多个角色同时显示。
- 拖动、点击互动、自动走动，以及随机待机、坐下和睡觉。有 `Special` 动作的模型在点击时有 20% 概率播放该动作。
- 通用缩放与帧率设置，以及每个模型独立的相对缩放倍率、显示器、行为和启停设置。
- 可选重力与窗口边框检测，支持站在窗口上沿或内部下沿，自动行走在边缘折返。
- 透明桌宠窗口，无窗口边框和背景模糊；管理窗口支持拖动、缩放和收起到托盘。
- 浅色、深色、跟随系统模式，预设主题色和 matugen 系统配色。
- 侧栏模型快捷开关，以及仅显示已开启模型的筛选选项。

## 环境要求

- Arch Linux，运行 Hyprland 0.55 或更新版本的 Wayland 会话。
- Qt 6.10+、GTK4 4.12+、gtk4-layer-shell、OpenGL 3.3。
- 托盘功能需要桌面面板提供托盘模块。

## 安装

下载或克隆本仓库后，在项目目录安装依赖：

```bash
sudo pacman -S --needed base-devel cmake pkgconf qt6-base qt6-declarative qt6-wayland qt6-svg gtk4 gtk4-layer-shell libepoxy hyprland noto-fonts-cjk
```

构建并安装 Arch 软件包：

```bash
./packaging/release.sh
cd outputs
makepkg -si
```

安装后从应用菜单打开 ArkPets，或运行 `arkpets`。

也可以在项目目录直接构建和运行：

```bash
./build.sh
./run.sh
```

如需安装到当前用户目录，运行 `./install.sh`，默认安装到 `~/.local`。第三方组件源码已包含在 `vendor/`，构建时无需下载组件或角色模型。

升级前先运行 `arkpets --quit`，完成安装后重新启动。已下载模型与设置会保留。

## 使用

1. 在“模型库”搜索角色或外观，下载后点击“显示到桌面”。已下载模型可以离线使用。
2. 在“通用设置”调整缩放倍率、帧率和默认行为。1 倍表示模型原始坐标的 1 单位对应 1 逻辑像素，保留不同模型的原始比例，切换动作时保持缩放不变。
3. 点击侧栏中的模型名称调整单独设置。相对缩放倍率与通用倍率相乘，其他选项可继承通用设置；选择“播放动作”会自动关闭该模型的自动活动，点击“保存并应用”后生效。
4. 左键点击角色触发互动，按住可拖动。移动范围以 `Default` 动作边界为准，特效可以超出屏幕；缺少有效 `Default` 时使用待机动作。互动结束后恢复原动作或自动活动。
5. 左下角“设置”管理面板主题和侧栏选项。“面板快捷调整”开启后，模型行右侧显示启停开关。

点击管理窗口右上角的叉号会收起到托盘，角色继续运行。通过托盘菜单退出程序，或使用：

```bash
arkpets --stop  # 停止全部角色，保留管理窗口
arkpets --quit  # 退出程序
```

选择 matugen 配色时，程序读取系统已有的颜色文件；未安装 matugen 或没有可用配色时使用默认颜色。支持 `~/.config/matugen/colors.json`、`~/.cache/matugen/colors.json` 和 `~/.cache/dms/dms-colors.json`。

## 数据位置

| 内容 | 默认路径 |
| --- | --- |
| 模型与索引缓存 | `~/.local/share/arkpets/` |
| 模型与面板设置 | `~/.config/arkpets/` |

遵循 `XDG_DATA_HOME` 和 `XDG_CONFIG_HOME`，也可通过 `--data-dir` 与 `--config-dir` 指定目录。

## 许可与来源

项目原创代码采用 [GPL-3.0-or-later](LICENSE)。Spine Runtime、Material 组件、图标字体及角色素材分别适用各自的许可，详见 [第三方声明](THIRD_PARTY.md) 和 [来源记录](SOURCES.json)。

本仓库和安装包不包含游戏模型，模型由用户按需下载。
