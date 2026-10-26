# 第三方来源与许可

## ArkPets Linux 与 Ark-Pets

本目录新增的原生窗口、渲染整合、Material 界面、下载器与打包脚本按 GPL-3.0-or-later 提供，全文见 `LICENSE`。

参考项目：[isHarryh/Ark-Pets](https://github.com/isHarryh/Ark-Pets)，Copyright (c) 2022-2026 Harry Huang and contributors，GPL-3.0。本程序使用兼容的模型格式；没有移植原项目的 Java 启动器。

## Spine C Runtime 3.8

来源：[EsotericSoftware/spine-runtimes](https://github.com/EsotericSoftware/spine-runtimes/tree/3.8/spine-c)，固定提交 `8b4844bd4b193ba9e54487ed397a777993cbad56`。

Copyright (c) 2013-2020, Esoteric Software LLC。未修改的源码与版权头保留在 `vendor/spine-c/`，独立许可全文见 `vendor/spine-c/LICENSE`，安装后为 `share/licenses/arkpets-linux/SPINE-LICENSE`。这部分不属于 GPL 代码授权。

整合、使用或分发 Spine Runtime 应满足其 Spine Editor License / Spine Runtimes License 条件；运行库的许可要求不会因本程序代码采用 GPL 而消失。本项目没有替使用者取得 Spine 授权。参见附带许可原文以及 [上游说明](https://github.com/EsotericSoftware/spine-runtimes/blob/3.8/spine-c/README.md)。

## 角色模型

软件与源码发布包不包含角色模型。用户选择下载时，程序从 [isHarryh/Ark-Models](https://github.com/isHarryh/Ark-Models) 读取 `models_data.json`，下载对应模型的 atlas、skel 和 PNG。

模型、贴图和角色素材版权归上海鹰角网络有限公司所有。模型仓库声明不得用于商业用途、不得损害版权方利益。下载素材不属于本程序的 GPL 授权。下载目录的 `complete.json` 记录来源、索引条目及本地文件 SHA-256。

## material-components-qml

管理界面使用 [sudoevolve/material-components-qml](https://github.com/sudoevolve/material-components-qml)，固定提交 `900a537824812bb592a761a0fd0b301422bd1a79`，QML 模块为 `md3.Core`。组件源码及 Google Material Color Utilities 保存在 `vendor/material-components-qml/`；未包含 Gallery 和其他示例。

组件的 `src/Core/Styles/Theme.qml` 声明 LGPL-3.0-or-later。原始版权头、GPL v3 / LGPL v3 许可文本及本地修改说明随源码提供。应用静态链接模块，配套源码包包含重新编译、修改和链接需要的完整源码与 CMake 构建文件。详见组件目录的 `NOTICE.md`。

本地修改包括：继承 Item 的禁用状态、TextField 的数值校验接口及编辑绑定、导航和按钮的键盘/无障碍交互、开关的 48 dp 点击区域，以及 ScrollBar 跨入列表时保持拖动、轨道点击后重设起点和稳定拖动尺寸的修复。布局、数字框组合和窗口拖动区域由应用自身实现，按钮、输入框、下拉菜单、开关、滑块、导航、卡片、弹窗的视觉来自组件库。

Google Material Color Utilities 与 Material Icons Round 图标字体按 Apache-2.0 提供。中文使用系统字体，Arch 包依赖 `noto-fonts-cjk`；不再附带旧库及 HarmonyOS 字体。安装后的声明位于 `share/licenses/arkpets-linux/material-components-qml/`。

## 系统图形库与测试协议

Qt 6、GTK4、GLib、GDK Pixbuf、libepoxy、gtk4-layer-shell 使用系统动态库，按各自许可提供，没有将其源码打包到本目录。Qt Quick 提供 QML 渲染，按钮、滑块、输入框、开关、标签页、卡片和弹窗等由 material-components-qml 提供；自有 SVG 图标属于新增代码资源。

`tests/protocol/wlr-virtual-pointer-unstable-v1.xml` 来自 wlroots 的 [wlr-protocols](https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/blob/master/unstable/wlr-virtual-pointer-unstable-v1.xml)，MIT 许可，原始版权声明保留在文件中。虚拟指针仅用于显式启用的桌面集成测试，不安装到用户系统。
