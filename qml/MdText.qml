import QtQuick
import md3.Core as MD

// The library has no label component; all text uses its MD3 type and color tokens.
Text {
    property string role: "bodyMedium"
    readonly property var typeStyle: MD.Theme.typography[role]
    font.family: typeStyle.family
    font.pixelSize: typeStyle.size
    font.weight: typeStyle.weight
    font.letterSpacing: typeStyle.tracking
    color: MD.Theme.color.onSurfaceColor
    verticalAlignment: Text.AlignVCenter
}
