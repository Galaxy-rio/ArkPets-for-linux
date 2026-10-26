import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import md3.Core as MD
import ArkPets 1.0

Window {
    id: root
    objectName: "mainWindow"
    width: 1120
    height: 800
    minimumWidth: 960
    minimumHeight: 700
    visible: true
    title: "ArkPets"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: MD.Theme.color.surface

    property string currentPage: "library"
    readonly property var controller: backend
    readonly property string currentPetId: currentPage.startsWith("pet:") ? currentPage.slice(4) : ""
    property string initialSearch: ""
    property string kindFilter: "Operator"
    property int draftSize: 280
    property int draftFps: 30
    property bool draftAutoActivity: false
    property bool draftClickThrough: false
    property bool draftFlip: false
    property string draftMonitor: ""
    property string draftLayer: "top"
    property string feedback: ""
    readonly property var pages: [
        {text: "模型库", icon: "widgets", page: "library"},
        {text: "已下载", icon: "download_done", page: "downloaded"},
        {type: "divider"},
        {text: "通用设置", icon: "tune", page: "model-settings"}
    ]
    readonly property var modelPages: backend.pets
        .filter(pet => !backend.panel.sidebarEnabledOnly || pet.enabled)
        .map(pet => ({text: pet.name, icon: "star", page: "pet:" + pet.id,
                     modelId: pet.id, checkable: true, checked: pet.enabled}))

    readonly property var bottomPages: [{text: "设置", icon: "settings", page: "settings"}]
    function applyAppearance() {
        let p = backend.panel
        MD.StyleManager.followSystemTheme = false
        MD.StyleManager.configure(p.seed, p.monochrome, p.lightScheme, p.darkScheme)
        MD.StyleManager.isDarkTheme = p.dark
    }
    Connections {target: backend.panel; function onChanged() {root.applyAppearance()}}
    Connections {
        target: backend
        function onPetsChanged() {
            if (root.currentPetId && !backend.pets.some(pet => pet.id === root.currentPetId))
                root.currentPage = "model-settings"
        }
    }

    function loadPreferences() {
        let p = backend.preferences
        draftSize = p.size; draftFps = p.fps
        draftAutoActivity = !!p.autoActivity
        draftClickThrough = p.clickThrough; draftFlip = p.flip
        draftMonitor = p.monitor; draftLayer = p.layer
        sizeField.text = String(draftSize); fpsField.text = String(draftFps)
    }
    function applyFilter() {
        backend.setFilter(search.text, currentPage === "downloaded" ? "" : kindFilter,
                          currentPage === "downloaded")
    }
    function saveSettings() {
        sizeField.commit(); fpsField.commit()
        backend.savePreferences({size: draftSize, fps: draftFps, autoActivity: draftAutoActivity,
                                 monitor: draftMonitor, layer: draftLayer,
                                 clickThrough: draftClickThrough, flip: draftFlip})
        if (!backend.error) {feedback = "已保存"; feedbackTimer.restart()}
    }
    onCurrentPageChanged: {
        feedback = ""
        if (currentPage === "model-settings" && sizeField) loadPreferences()
        else if (search && (currentPage === "library" || currentPage === "downloaded")) applyFilter()
    }
    Component.onCompleted: {
        applyAppearance()
        loadPreferences()
        applyFilter()
    }
    onClosing: function(close) {close.accepted = false; root.hide()}
    Shortcut {sequence: "Ctrl+W"; onActivated: root.close()}
    Timer {id: feedbackTimer; interval: 2500; onTriggered: root.feedback = ""}

    MD.NavigationDrawer {
        id: navigation
        objectName: "sidebarNavigation"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: drawerWidth
        drawerWidth: 256
        modal: false
        title: "ArkPets"
        headerModel: root.pages
        model: root.modelPages
        footerModel: root.bottomPages
        itemSwitchesVisible: backend.panel.sidebarQuickAdjust
        Binding {target: navigation; property: "currentItem"; value: root.currentPage === "settings" ? root.bottomPages[0] : root.pages.concat(root.modelPages).find(item => item.page === root.currentPage) || null}
        onItemClicked: function(itemData) {if (itemData.page) root.currentPage = itemData.page}
        onItemToggled: function(itemData, checked) {backend.setPetEnabled(itemData.modelId, checked)}
    }

    // No painted bar or separator: the existing page surfaces continue to the top.
    MouseArea {
        id: windowDragArea
        objectName: "windowDragArea"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: hideToTray.left
        height: 64
        onPressed: root.startSystemMove()
    }
    MD.IconButton {
        id: hideToTray
        objectName: "hideToTrayButton"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.rightMargin: 12
        width: 48; height: 48
        icon: "close"
        Accessible.name: "收起到托盘"
        onClicked: root.close()
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: navigation.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: 80
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        anchors.bottomMargin: 24
        spacing: 24

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            MdText {role: "headlineMedium"; text: root.currentPetId ? "模型设置" : root.currentPage === "settings" ? "设置" : root.currentPage === "model-settings" ? "通用设置" : root.currentPage === "downloaded" ? "已下载" : "模型库"}
            Item {Layout.fillWidth: true}
            MD.Button {
                objectName: "stopPet"
                type: "text"; text: "停止全部"; icon: "stop"
                visible: backend.petRunning
                onClicked: backend.stopPet()
            }
        }

        ColumnLayout {
            visible: root.currentPage === "library" || root.currentPage === "downloaded"
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                MD.TextField {
                    id: search; objectName: "searchField"
                    Layout.fillWidth: true
                    type: "outlined"
                    label: "搜索角色、英文名或时装"
                    text: root.initialSearch
                    leadingIcon: "search"
                    trailingIcon: text.length ? "close" : ""
                    onTrailingIconClicked: text = ""
                    onTextChanged: searchTimer.restart()
                    Timer {id: searchTimer; interval: 140; onTriggered: root.applyFilter()}
                }
                MD.IconButton {
                    width: 48; height: 48
                    icon: "refresh"
                    Accessible.name: "刷新模型列表"
                    enabled: !backend.busy
                    onClicked: backend.refreshCatalog()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                MD.Tabs {
                    id: categoryTabs
                    visible: root.currentPage === "library"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    type: "secondary"
                    model: [{text: "基建小人"}, {text: "敌人"}, {text: "动态立绘"}, {text: "全部"}]
                    onCurrentIndexChanged: {
                        root.kindFilter = ["Operator", "Enemy", "DynIllust", ""][currentIndex]
                        if (search) root.applyFilter()
                    }
                }
                MdText {text: backend.visibleCount + " 个模型"; color: MD.Theme.color.onSurfaceVariantColor}
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 20
                ListView {
                    id: modelList; objectName: "modelList"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    readonly property int rowHeight: 80
                    model: backend.models
                    boundsBehavior: Flickable.StopAtBounds
                    ListWheelHandler {
                        objectName: "modelWheelHandler"
                        parent: modelList
                        anchors.fill: parent
                        z: 1
                        target: modelList
                        step: modelList.rowHeight + modelList.spacing
                    }
                    MD.ScrollBar {objectName: "modelScrollBar"; parent: modelList; target: modelList; anchors.right: parent.right; height: parent.height; z: 2}
                    delegate: MD.Card {
                        id: modelCard
                        required property string modelId
                        required property string modelName
                        required property string englishName
                        required property string skinName
                        required property bool available
                        required property bool downloaded
                        readonly property bool selected: backend.selected.id === modelId
                        width: ListView.view.width - 8
                        height: ListView.view.rowHeight
                        padding: 16
                        type: "filled"
                        color: selected ? MD.Theme.color.secondaryContainer : MD.Theme.color.surfaceContainerLow
                        activeFocusOnTab: true
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelName + " " + skinName
                        Accessible.selected: selected
                        Keys.onReturnPressed: backend.selectModel(modelId)
                        Keys.onSpacePressed: backend.selectModel(modelId)
                        MD.Ripple {
                            anchors.fill: parent; anchors.margins: -16
                            clipRadius: modelCard.radius
                            onClicked: {modelCard.forceActiveFocus(); backend.selectModel(modelCard.modelId)}
                        }
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 4
                            MdText {
                                role: "titleMedium"
                                Layout.fillWidth: true
                                text: modelCard.modelName + (modelCard.englishName ? " · " + modelCard.englishName : "")
                                elide: Text.ElideRight
                                color: modelCard.selected ? MD.Theme.color.onSecondaryContainerColor : MD.Theme.color.onSurfaceColor
                            }
                            MdText {
                                Layout.fillWidth: true
                                text: modelCard.skinName + (modelCard.downloaded ? " · 已下载" : !modelCard.available ? " · 资源缺失" : "")
                                color: MD.Theme.color.onSurfaceVariantColor
                                elide: Text.ElideRight
                            }
                        }
                    }
                    MdText {
                        anchors.centerIn: parent
                        text: backend.busy ? "正在加载列表…" : backend.totalCount === 0 ? "暂无列表，请刷新" : "没有匹配的模型"
                        visible: modelList.count === 0
                        color: MD.Theme.color.onSurfaceVariantColor
                    }
                }
                MD.Card {
                    type: "filled"
                    color: MD.Theme.color.surfaceContainerLow
                    padding: 20
                    Layout.preferredWidth: 252
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 16
                        MdText {role: "titleLarge"; text: backend.selected.name || "选择模型"; Layout.fillWidth: true; wrapMode: Text.Wrap}
                        MdText {text: backend.selected.english || ""; visible: text.length > 0; color: MD.Theme.color.onSurfaceVariantColor}
                        MdText {text: backend.selected.skin || ""; Layout.fillWidth: true; wrapMode: Text.Wrap}
                        MdText {
                            text: !backend.selected.id ? "" : backend.selected.downloaded ? "已下载" : backend.selected.available ? "未下载" : "上游资源不完整"
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            color: MD.Theme.color.onSurfaceVariantColor
                        }
                        Item {Layout.fillHeight: true}
                        MdText {text: "自动活动已开启"; visible: backend.selected.downloaded === true && backend.selected.autoActivity; color: MD.Theme.color.primary}
                        MD.ComboBox {
                            id: animationPicker
                            Layout.fillWidth: true
                            label: "播放动作"
                            labelBackgroundColor: MD.Theme.color.surfaceContainerLow
                            type: "outlined"
                            visible: backend.selected.downloaded === true
                            enabled: !backend.selected.autoActivity
                            model: backend.animations
                            onModelChanged: {let i = backend.animations.indexOf("Relax"); currentIndex = i >= 0 ? i : 0}
                        }
                        MD.Button {
                            objectName: "primaryAction"
                            Layout.fillWidth: true; Layout.preferredHeight: 48
                            text: backend.selected.downloaded ? backend.selected.enabled ? "应用所选动作" : "显示到桌面" : "下载模型"
                            icon: backend.selected.downloaded ? "play_arrow" : "download"
                            enabled: !!backend.selected.available && !backend.busy && (!backend.selected.downloaded || backend.animations.length > 0)
                            onClicked: backend.selected.downloaded ? backend.startPet(animationPicker.currentText) : backend.downloadSelected()
                        }
                        MD.Button {
                            type: "text"
                            Layout.fillWidth: true
                            text: "删除模型"
                            visible: backend.selected.downloaded === true
                            enabled: !backend.busy
                            onClicked: deleteDialog.open()
                        }
                    }
                }
            }
        }

        PanelSettings {
            visible: root.currentPage === "settings"
            Layout.fillWidth: true; Layout.fillHeight: true
            settings: backend.panel
        }

        ModelSettings {
            id: individualSettings; objectName: "individualSettings"
            visible: root.currentPetId.length > 0
            Layout.fillWidth: true; Layout.fillHeight: true
            backend: root.controller
            modelId: root.currentPetId
        }

        Flickable {
            id: settingsScroll
            visible: root.currentPage === "model-settings"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: settingsContent.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            MD.ScrollBar {objectName: "settingsScrollBar"; parent: settingsScroll; target: settingsScroll; anchors.right: parent.right; height: parent.height; z: 2}
            ColumnLayout {
                id: settingsContent
                width: settingsScroll.width - 8
                spacing: 12
                MdText {role: "titleMedium"; text: "显示"; color: MD.Theme.color.primary}
                RowLayout {
                    Layout.fillWidth: true; spacing: 24
                    MdText {text: "人物高度"; role: "bodyLarge"; Layout.preferredWidth: 112}
                    MD.Slider {
                        id: sizeSlider; objectName: "sizeSlider"
                        Layout.fillWidth: true
                        from: 96; to: 800; stepSize: 8
                        Binding {target: sizeSlider; property: "value"; value: root.draftSize}
                        onPressedChanged: if (pressed) forceActiveFocus()
                        onMoved: root.draftSize = Math.round(value)
                    }
                    NumberField {
                        id: sizeField; objectName: "sizeField"
                        Layout.preferredWidth: 128
                        label: "逻辑像素"
                        from: 96; to: 800
                        Binding {target: sizeField; property: "value"; value: root.draftSize}
                        onValueModified: function(value) {root.draftSize = value}
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 24
                    MdText {text: "帧率"; role: "bodyLarge"; Layout.preferredWidth: 112}
                    MD.Slider {
                        id: fpsSlider; objectName: "fpsSlider"
                        Layout.fillWidth: true
                        from: 1; to: 120; stepSize: 1
                        Binding {target: fpsSlider; property: "value"; value: root.draftFps}
                        onPressedChanged: if (pressed) forceActiveFocus()
                        onMoved: root.draftFps = Math.round(value)
                    }
                    NumberField {
                        id: fpsField; objectName: "fpsField"
                        Layout.preferredWidth: 128
                        label: "FPS"
                        from: 1; to: 120
                        Binding {target: fpsField; property: "value"; value: root.draftFps}
                        onValueModified: function(value) {root.draftFps = value}
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 20
                    MD.ComboBox {
                        id: monitorPicker
                        Layout.fillWidth: true
                        type: "outlined"; label: "显示器"
                        model: ["自动选择"].concat(backend.monitors)
                        labelBackgroundColor: MD.Theme.color.surface
                        Binding {target: monitorPicker; property: "currentIndex"; value: root.draftMonitor ? Math.max(0, backend.monitors.indexOf(root.draftMonitor) + 1) : 0}
                        onActivated: function(index) {root.draftMonitor = index === 0 ? "" : currentText}
                    }
                    MD.ComboBox {
                        id: layerPicker; objectName: "layerPicker"
                        Layout.fillWidth: true
                        type: "outlined"; label: "显示层级"
                        model: ["普通窗口之上", "桌面层", "覆盖全屏窗口"]
                        labelBackgroundColor: MD.Theme.color.surface
                        Binding {target: layerPicker; property: "currentIndex"; value: ["top", "bottom", "overlay"].indexOf(root.draftLayer)}
                        onActivated: function(index) {root.draftLayer = ["top", "bottom", "overlay"][index]}
                    }
                }
                Rectangle {height: 1; Layout.fillWidth: true; Layout.topMargin: 4; Layout.bottomMargin: 4; color: MD.Theme.color.outlineVariant}
                MdText {role: "titleMedium"; text: "行为"; color: MD.Theme.color.primary}
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 48
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        MdText {text: "自动活动"; role: "bodyLarge"}
                        MdText {text: "随机走动、待机、坐下和睡觉"; color: MD.Theme.color.onSurfaceVariantColor}
                    }
                    Item {Layout.fillWidth: true}
                    MD.Switch {
                        Layout.preferredWidth: 52; Layout.maximumWidth: 52; Layout.preferredHeight: 48
                        id: autoActivitySwitch; objectName: "autoActivitySwitch"
                        Accessible.name: "自动活动"
                        Binding {target: autoActivitySwitch; property: "checked"; value: root.draftAutoActivity}
                        onClicked: root.draftAutoActivity = checked
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 48
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        MdText {text: "允许拖动"; role: "bodyLarge"}
                        MdText {text: "关闭后鼠标穿透角色"; color: MD.Theme.color.onSurfaceVariantColor}
                    }
                    Item {Layout.fillWidth: true}
                    MD.Switch {
                        Layout.preferredWidth: 52; Layout.maximumWidth: 52; Layout.preferredHeight: 48
                        id: dragSwitch
                        Accessible.name: "允许拖动"
                        Binding {target: dragSwitch; property: "checked"; value: !root.draftClickThrough}
                        onClicked: root.draftClickThrough = !checked
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 48
                    MdText {text: "水平翻转"; role: "bodyLarge"; Layout.fillWidth: true}
                    MD.Button {type: "text"; text: "重置所有位置"; onClicked: backend.resetPosition()}
                    Item {Layout.fillWidth: true}
                    MD.Switch {
                        Layout.preferredWidth: 52; Layout.maximumWidth: 52; Layout.preferredHeight: 48
                        id: flipSwitch
                        Accessible.name: "水平翻转"
                        Binding {target: flipSwitch; property: "checked"; value: root.draftFlip}
                        onClicked: root.draftFlip = checked
                    }
                }
            }
        }
        RowLayout {
            visible: root.currentPage === "model-settings"
            Layout.fillWidth: true; spacing: 16
            MD.Button {type: "text"; text: "模型文件夹"; icon: "folder_open"; onClicked: backend.openDataDirectory()}
            MdText {text: root.feedback; color: MD.Theme.color.primary}
            Item {Layout.fillWidth: true}
            MD.Button {type: "text"; text: "放弃修改"; onClicked: root.loadPreferences()}
            MD.Button {objectName: "saveSettings"; text: "保存并应用"; Layout.preferredHeight: 48; onClicked: root.saveSettings()}
        }
        ColumnLayout {
            visible: backend.busy || backend.error.length > 0
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                MdText {
                    text: backend.error || backend.status
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    maximumLineCount: 3; elide: Text.ElideRight
                    color: backend.error ? MD.Theme.color.error : MD.Theme.color.onSurfaceColor
                }
                MD.Button {type: "text"; text: "取消"; visible: backend.downloadId.length > 0; onClicked: backend.cancelDownload()}
                MD.IconButton {icon: "close"; Accessible.name: "清除错误"; visible: backend.error.length > 0; onClicked: backend.clearError()}
            }
            MD.LinearProgress {Layout.fillWidth: true; visible: backend.busy; value: backend.progress; indeterminate: backend.downloadId.length === 0}
        }
    }

    WindowResizeHandles {anchors.fill: parent; window: root}
    MD.Dialog {
        id: deleteDialog
        title: "删除本地模型？"
        text: (backend.selected.name || "") + " · " + (backend.selected.skin || "")
        acceptText: "删除"; rejectText: "取消"
        onAccepted: backend.removeSelected()
    }
}
