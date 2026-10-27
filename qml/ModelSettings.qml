import QtQuick
import QtQuick.Layouts
import md3.Core as MD
import ArkPets 1.0

ColumnLayout {
    id: page
    required property var backend
    property string modelId: ""
    property var profile: ({})
    property var animations: []
    property real draftMultiplier: 1
    property var draftOverrides: ({})
    property string draftAnimation: ""
    property string feedback: ""
    readonly property var monitorNames: [""].concat(backend.monitors).concat(
        draftOverrides.monitor && backend.monitors.indexOf(draftOverrides.monitor) < 0 ? [draftOverrides.monitor] : [])
    spacing: 16

    function refreshState() {profile = modelId ? backend.modelSettings(modelId) : ({})}
    function load() {
        feedback = ""
        refreshState()
        draftMultiplier = profile.multiplier || 1
        draftOverrides = Object.assign({}, profile.overrides || {})
        animations = modelId ? backend.modelAnimations(modelId) : []
        let saved = profile.animation || ""
        let relax = animations.find(name => name.toLowerCase() === "relax")
        draftAnimation = animations.indexOf(saved) >= 0 ? saved : relax || animations[0] || ""
        multiplierField.text = String(draftMultiplier)
    }
    function effective(key) {
        return draftOverrides[key] !== undefined ? draftOverrides[key] : backend.preferences[key]
    }
    function setOverride(key, value) {
        let next = Object.assign({}, draftOverrides)
        if (value === undefined) delete next[key]
        else next[key] = value
        draftOverrides = next
    }
    function booleanIndex(key) {return draftOverrides[key] === undefined ? 0 : draftOverrides[key] ? 1 : 2}
    function booleanOptions(key) {return ["跟随通用设置（" + (backend.preferences[key] ? "开启" : "关闭") + "）", "开启", "关闭"]}
    function setBoolean(key, index) {setOverride(key, index === 0 ? undefined : index === 1)}
    function save() {
        multiplierField.commit()
        backend.saveModelSettings(modelId, {multiplier: draftMultiplier, overrides: draftOverrides, animation: draftAnimation})
        if (!backend.error) {feedback = "已保存"; feedbackTimer.restart()}
    }
    onModelIdChanged: if (multiplierField) load()
    Component.onCompleted: load()
    Connections {target: page.backend; function onPetsChanged() {page.refreshState()}}
    Timer {id: feedbackTimer; interval: 2500; onTriggered: page.feedback = ""}

    RowLayout {
        Layout.fillWidth: true; spacing: 16
        ColumnLayout {
            Layout.fillWidth: true; spacing: 4
            MdText {objectName: "individualModelName"; text: page.profile.name || ""; role: "titleLarge"; Layout.fillWidth: true; elide: Text.ElideRight}
            MdText {text: page.profile.skin || ""; color: MD.Theme.color.onSurfaceVariantColor; Layout.fillWidth: true; wrapMode: Text.WordWrap}
        }
        MdText {text: page.profile.ready ? "已显示" : page.profile.enabled ? "正在启动" : "已关闭"; color: MD.Theme.color.onSurfaceVariantColor}
        MD.Switch {
            id: enabledSwitch; objectName: "modelEnabledSwitch"
            Layout.preferredWidth: 52; Layout.preferredHeight: 48
            Accessible.name: "显示此模型"
            Binding {target: enabledSwitch; property: "checked"; value: page.profile.enabled === true}
            onClicked: page.backend.setPetEnabled(page.modelId, checked)
        }
    }

    Flickable {
        id: scroll; objectName: "individualSettingsScroll"
        Layout.fillWidth: true; Layout.fillHeight: true
        contentWidth: width; contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds; clip: true
        ListWheelHandler {
            objectName: "individualSettingsWheelHandler"
            parent: scroll; anchors.fill: parent; z: 1; target: scroll
        }
        MD.ScrollBar {parent: scroll; target: scroll; anchors.right: parent.right; height: parent.height; z: 2}
        ColumnLayout {
            id: content
            width: scroll.width - 8; spacing: 20
            MdText {text: "显示"; role: "titleMedium"; color: MD.Theme.color.primary}
            RowLayout {
                Layout.fillWidth: true; spacing: 20
                MdText {text: "相对缩放倍率"; role: "bodyLarge"; Layout.preferredWidth: 112}
                MD.Slider {
                    id: multiplierSlider; objectName: "multiplierSlider"
                    Layout.fillWidth: true; from: 0.25; to: 2.5; stepSize: 0.05
                    Binding {target: multiplierSlider; property: "value"; value: page.draftMultiplier}
                    onPressedChanged: if (pressed) forceActiveFocus()
                    onMoved: page.draftMultiplier = Math.round(value * 100) / 100
                }
                MultiplierField {
                    id: multiplierField; objectName: "multiplierField"
                    Layout.preferredWidth: 128
                    Binding {target: multiplierField; property: "value"; value: page.draftMultiplier}
                    onValueModified: function(value) {page.draftMultiplier = value}
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 20
                MD.ComboBox {
                    id: monitor; objectName: "individualMonitorPicker"
                    type: "outlined"; label: "显示器"; Layout.fillWidth: true
                    labelBackgroundColor: MD.Theme.color.surface
                    model: ["跟随通用设置"].concat(page.monitorNames.map(name => name || "自动选择"))
                    Binding {target: monitor; property: "currentIndex"; value: page.draftOverrides.monitor === undefined ? 0 : page.monitorNames.indexOf(page.draftOverrides.monitor) + 1}
                    onActivated: function(index) {page.setOverride("monitor", index === 0 ? undefined : page.monitorNames[index - 1])}
                }
                MD.ComboBox {
                    id: layer; objectName: "individualLayerPicker"
                    type: "outlined"; label: "显示层级"; Layout.fillWidth: true
                    labelBackgroundColor: MD.Theme.color.surface
                    model: ["跟随通用设置", "普通窗口之上", "桌面层", "覆盖全屏窗口"]
                    Binding {target: layer; property: "currentIndex"; value: page.draftOverrides.layer === undefined ? 0 : ["top", "bottom", "overlay"].indexOf(page.draftOverrides.layer) + 1}
                    onActivated: function(index) {page.setOverride("layer", index === 0 ? undefined : ["top", "bottom", "overlay"][index - 1])}
                }
            }
            Rectangle {Layout.fillWidth: true; height: 1; color: MD.Theme.color.outlineVariant}
            MdText {text: "行为"; role: "titleMedium"; color: MD.Theme.color.primary}
            RowLayout {
                Layout.fillWidth: true; spacing: 20
                MD.ComboBox {
                    id: activity; objectName: "individualActivityPicker"
                    type: "outlined"; label: "自动活动"; Layout.fillWidth: true
                    labelBackgroundColor: MD.Theme.color.surface
                    model: page.booleanOptions("autoActivity")
                    Binding {target: activity; property: "currentIndex"; value: page.booleanIndex("autoActivity")}
                    onActivated: function(index) {page.setBoolean("autoActivity", index)}
                }
                MD.ComboBox {
                    id: animation; objectName: "individualAnimationPicker"
                    type: "outlined"; label: "播放动作"; Layout.fillWidth: true
                    labelBackgroundColor: MD.Theme.color.surface
                    model: page.animations; enabled: page.animations.length > 0
                    Binding {target: animation; property: "currentIndex"; value: page.animations.indexOf(page.draftAnimation)}
                    onActivated: function(index) {
                        page.draftAnimation = page.animations[index]
                        page.setOverride("autoActivity", false)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 20
                MD.ComboBox {
                    id: clickThrough; objectName: "individualPointerPicker"
                    type: "outlined"; label: "鼠标穿透"; Layout.fillWidth: true
                    labelBackgroundColor: MD.Theme.color.surface
                    model: page.booleanOptions("clickThrough")
                    Binding {target: clickThrough; property: "currentIndex"; value: page.booleanIndex("clickThrough")}
                    onActivated: function(index) {page.setBoolean("clickThrough", index)}
                }
                MD.ComboBox {
                    id: flip; objectName: "individualFlipPicker"
                    type: "outlined"; label: "水平翻转"; Layout.fillWidth: true
                    labelBackgroundColor: MD.Theme.color.surface
                    model: page.booleanOptions("flip")
                    Binding {target: flip; property: "currentIndex"; value: page.booleanIndex("flip")}
                    onActivated: function(index) {page.setBoolean("flip", index)}
                }
            }
            MdText {text: "鼠标穿透关闭时，可以点击互动、按住拖动。"; role: "bodySmall"; color: MD.Theme.color.onSurfaceVariantColor; Layout.fillWidth: true; wrapMode: Text.WordWrap}
            RowLayout {
                Layout.fillWidth: true
                MD.Button {
                    objectName: "resetIndividualSettings"; type: "text"; text: "恢复通用设置"
                    onClicked: {page.draftMultiplier = 1; page.draftOverrides = ({}); multiplierField.text = "1"}
                }
                Item {Layout.fillWidth: true}
                MD.Button {objectName: "resetIndividualPosition"; type: "text"; text: "重置位置"; onClicked: page.backend.resetPosition(page.modelId)}
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 16
        MdText {text: page.feedback; color: MD.Theme.color.primary}
        Item {Layout.fillWidth: true}
        MD.Button {objectName: "discardIndividualSettings"; type: "text"; text: "放弃修改"; onClicked: page.load()}
        MD.Button {objectName: "saveIndividualSettings"; text: "保存并应用"; Layout.preferredHeight: 48; onClicked: page.save()}
    }
}
