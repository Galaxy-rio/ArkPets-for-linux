import QtQuick
import QtQuick.Layouts
import md3.Core as MD
import ArkPets 1.0

Flickable {
    id: page
    objectName: "panelSettingsPage"
    required property var settings
    clip: true
    contentWidth: width
    contentHeight: content.implicitHeight + 8
    boundsBehavior: Flickable.StopAtBounds
    ListWheelHandler {
        objectName: "panelSettingsWheelHandler"
        parent: page; anchors.fill: parent; z: 1; target: page
    }
    MD.ScrollBar {parent: page; target: page; anchors.right: parent.right; height: parent.height; z: 2}
    ColumnLayout {
        id: content
        width: page.width - 16
        spacing: 20
        MdText {text: "外观"; role: "titleMedium"; color: MD.Theme.color.primary}
        RowLayout {
            Layout.fillWidth: true; spacing: 24
            MdText {text: "模式"; role: "bodyLarge"; Layout.preferredWidth: 72}
            MD.SegmentedButton {
                id: modePicker; objectName: "themeModePicker"
                Layout.fillWidth: true; Layout.preferredHeight: 48
                Binding {
                    target: modePicker; property: "buttons"
                    value: [{text: "浅色", icon: "light_mode", selected: page.settings.mode === "light"},
                            {text: "深色", icon: "dark_mode", selected: page.settings.mode === "dark"},
                            {text: "跟随系统", icon: "contrast", selected: page.settings.mode === "system"}]
                }
                onClicked: function(index) {page.settings.mode = ["light", "dark", "system"][index]}
            }
        }
        Rectangle {Layout.fillWidth: true; height: 1; color: MD.Theme.color.outlineVariant}
        RowLayout {
            Layout.fillWidth: true; spacing: 24
            MdText {text: "配色方案"; role: "titleMedium"; color: MD.Theme.color.primary; Layout.fillWidth: true}
            MD.SegmentedButton {
                id: paletteSource; objectName: "paletteSourcePicker"
                Layout.preferredHeight: 48
                Binding {
                    target: paletteSource; property: "buttons"
                    value: [{text: "预设颜色", icon: "palette", selected: page.settings.palette !== "matugen"},
                            {text: "matugen", icon: "auto_awesome", selected: page.settings.palette === "matugen"}]
                }
                onClicked: function(index) {page.settings.palette = index === 1 ? "matugen" : "green"}
            }
        }
        GridLayout {
            visible: page.settings.palette !== "matugen"
            Layout.fillWidth: true
            columns: 5; rowSpacing: 12; columnSpacing: 12
            Repeater {
                model: page.settings.presets
                delegate: MD.Card {
                    id: swatch
                    required property var modelData
                    objectName: "palette-" + modelData.id
                    readonly property bool selected: page.settings.palette === modelData.id
                    readonly property var preview: MD.StyleManager.preview(modelData.seed, page.settings.dark, modelData.id === "monochrome")
                    Layout.fillWidth: true; Layout.preferredHeight: 112
                    Layout.preferredWidth: 120; Layout.minimumWidth: 88
                    padding: 12
                    type: "outlined"
                    outlineColor: selected || activeFocus ? MD.Theme.color.primary : "transparent"
                    color: selected ? MD.Theme.color.secondaryContainer : MD.Theme.color.surfaceContainerLow
                    activeFocusOnTab: true
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.label
                    Accessible.checked: selected
                    function choose() {forceActiveFocus(); page.settings.palette = modelData.id}
                    Keys.onSpacePressed: choose()
                    Keys.onReturnPressed: choose()
                    MD.Ripple {anchors.fill: parent; anchors.margins: -12; clipRadius: 12; onClicked: swatch.choose()}
                    Column {
                        anchors.centerIn: parent; spacing: 8
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 48; height: 48
                            // A static palette preview, inside the library's Card/Ripple.
                            Rectangle {
                                anchors.fill: parent; radius: 24; color: swatch.preview.primary
                                Item {
                                    x: 0; y: 24; width: 24; height: 24
                                    clip: true
                                    Rectangle {y: -24; width: 48; height: 48; radius: 24; color: swatch.preview.primaryContainer}
                                }
                                Item {
                                    x: 24; y: 24; width: 24; height: 24
                                    clip: true
                                    Rectangle {x: -24; y: -24; width: 48; height: 48; radius: 24; color: swatch.preview.tertiary}
                                }
                            }
                        }
                        MdText {anchors.horizontalCenter: parent.horizontalCenter; text: swatch.modelData.label; role: "labelLarge"}
                    }
                    Text {
                        anchors.right: parent.right; anchors.top: parent.top
                        text: "check_circle"; font.family: MD.Theme.iconFont.name; font.pixelSize: 18
                        visible: swatch.selected; color: MD.Theme.color.primary
                    }
                }
            }
        }
        MD.Card {
            visible: page.settings.palette === "matugen"
            Layout.fillWidth: true; Layout.preferredHeight: 144
            type: "filled"; color: MD.Theme.color.surfaceContainerLow
            ColumnLayout {
                anchors.fill: parent; spacing: 12
                MdText {text: "使用系统生成的 Material 配色"; role: "titleMedium"}
                MdText {objectName: "matugenStatus"; text: page.settings.systemStatus; Layout.fillWidth: true; wrapMode: Text.Wrap; color: MD.Theme.color.onSurfaceVariantColor}
                MD.Button {type: "text"; text: "重新读取"; icon: "refresh"; onClicked: page.settings.refreshSystemPalette()}
            }
        }
        MdText {
            visible: page.settings.error.length > 0
            text: page.settings.error
            color: MD.Theme.color.error
            Layout.fillWidth: true; wrapMode: Text.Wrap
        }
        Rectangle {Layout.fillWidth: true; height: 1; color: MD.Theme.color.outlineVariant}
        MdText {text: "侧边栏"; role: "titleMedium"; color: MD.Theme.color.primary}
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 48
            MdText {text: "面板快捷调整"; role: "bodyLarge"; Layout.fillWidth: true}
            MD.Switch {
                id: quickAdjust; objectName: "sidebarQuickAdjustSwitch"
                Layout.preferredWidth: 52; Layout.preferredHeight: 48
                Accessible.name: "面板快捷调整"
                Binding {target: quickAdjust; property: "checked"; value: page.settings.sidebarQuickAdjust}
                onClicked: page.settings.sidebarQuickAdjust = checked
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 48
            MdText {text: "仅显示开启的模型"; role: "bodyLarge"; Layout.fillWidth: true}
            MD.Switch {
                id: enabledOnly; objectName: "sidebarEnabledOnlySwitch"
                Layout.preferredWidth: 52; Layout.preferredHeight: 48
                Accessible.name: "仅显示开启的模型"
                Binding {target: enabledOnly; property: "checked"; value: page.settings.sidebarEnabledOnly}
                onClicked: page.settings.sidebarEnabledOnly = checked
            }
        }
    }
}
