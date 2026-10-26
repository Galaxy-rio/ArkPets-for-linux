import QtQuick

Item {
    required property var window
    id: handles
    // Invisible client-side resize targets; Qt forwards the gesture to Wayland.
    Repeater {
        model: [Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge, Qt.BottomEdge,
                Qt.LeftEdge | Qt.TopEdge, Qt.RightEdge | Qt.TopEdge,
                Qt.LeftEdge | Qt.BottomEdge, Qt.RightEdge | Qt.BottomEdge]
        MouseArea {
            required property int modelData
            readonly property bool horizontal: !!(modelData & (Qt.LeftEdge | Qt.RightEdge))
            readonly property bool vertical: !!(modelData & (Qt.TopEdge | Qt.BottomEdge))
            width: horizontal ? 6 : handles.width - 12
            height: vertical ? 6 : handles.height - 12
            x: modelData & Qt.RightEdge ? handles.width - width : horizontal ? 0 : 6
            y: modelData & Qt.BottomEdge ? handles.height - height : vertical ? 0 : 6
            cursorShape: horizontal && vertical
                         ? ((modelData === (Qt.LeftEdge | Qt.TopEdge) || modelData === (Qt.RightEdge | Qt.BottomEdge)) ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
                         : horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: handles.window.startSystemResize(modelData)
        }
    }
}
