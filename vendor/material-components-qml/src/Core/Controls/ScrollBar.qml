// Modified by ArkPets Linux, 2026-10-26. See NOTICE.md for drag interaction fixes.
import QtQuick
import md3.Core

// MD3 Style ScrollBar
Rectangle {
    id: scrollBarTrack
    
    // API
    property Flickable target: null
    property int orientation: Qt.Vertical // Qt.Vertical or Qt.Horizontal
    
    // Styling
    property color trackColor: "transparent"
    property color thumbColor: Theme.color.outline
    property color thumbPressedColor: Theme.color.primary
    property real thumbOpacity: 0.8
    property real fadeDuration: 200
    
    // Dimensions
    // Keep the input area fixed while the visual thumb changes thickness.
    implicitWidth: orientation === Qt.Vertical ? 8 : parent.width
    implicitHeight: orientation === Qt.Vertical ? parent.height : 8
    
    // Visibility logic
    visible: target && (orientation === Qt.Vertical ? target.contentHeight > target.height : target.contentWidth > target.width)
    color: trackColor
    
    // Internal state
    property bool isPressed: scrollMouseArea.pressed
    property bool isMoving: target && target.moving
    property bool isHovered: scrollMouseArea.containsMouse
    

    // Thumb Component
    Rectangle {
        id: scrollBarThumb
        
        // Dimensions & Position
        x: orientation === Qt.Vertical ? (parent.width - width) / 2 : calculatePosition()
        y: orientation === Qt.Vertical ? calculatePosition() : (parent.height - height) / 2
        
        width: orientation === Qt.Vertical ? (isPressed ? 4 : 6) : calculateSize()
        height: orientation === Qt.Vertical ? calculateSize() : (isPressed ? 4 : 6)
        
        color: isPressed ? thumbPressedColor : thumbColor
        radius: (orientation === Qt.Vertical ? width : height) / 2
        
        opacity: (isHovered || isPressed || isMoving) ? thumbOpacity : 0.0
        
        Behavior on opacity { NumberAnimation { duration: fadeDuration } }
        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on width { enabled: orientation === Qt.Vertical; NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        Behavior on height { enabled: orientation === Qt.Horizontal; NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

        function calculateSize() {
            if (!target) return 40
            
            var baseSize = 0
            if (orientation === Qt.Vertical) {
                var ratio = target.height / target.contentHeight
                baseSize = Math.max(40, ratio * scrollBarTrack.height)
            } else {
                var ratio = target.width / target.contentWidth
                baseSize = Math.max(40, ratio * scrollBarTrack.width)
            }
            
            // A stable thumb length keeps pointer-to-content mapping constant.
            return Math.min(baseSize, orientation === Qt.Vertical ? scrollBarTrack.height : scrollBarTrack.width)
        }
        
        function calculatePosition() {
            if (!target) return 0
            
            if (orientation === Qt.Vertical) {
                if (target.contentHeight <= target.height) return 0
                var maxThumbY = scrollBarTrack.height - height
                var scrollRatio = target.contentY / (target.contentHeight - target.height)
                return Math.max(0, Math.min(scrollRatio * maxThumbY, maxThumbY))
            } else {
                if (target.contentWidth <= target.width) return 0
                var maxThumbX = scrollBarTrack.width - width
                var scrollRatio = target.contentX / (target.contentWidth - target.width)
                return Math.max(0, Math.min(scrollRatio * maxThumbX, maxThumbX))
            }
        }
    }

    MouseArea {
        id: scrollMouseArea
        anchors.fill: parent
        hoverEnabled: true
        // Keep the gesture even when the pointer crosses into list content.
        preventStealing: true
        scrollGestureEnabled: false
        
        property real pressedPos: 0
        property real initialContentPos: 0
        
        onPressed: (mouse) => {
            if (!target) return
            target.cancelFlick()
            
            var mousePos = (orientation === Qt.Vertical) ? mouse.y : mouse.x
            var thumbPos = (orientation === Qt.Vertical) ? scrollBarThumb.y : scrollBarThumb.x
            var thumbSize = (orientation === Qt.Vertical) ? scrollBarThumb.height : scrollBarThumb.width
            
            if (mousePos < thumbPos || mousePos > thumbPos + thumbSize) {
                // Clicked on track: Jump
                var trackSize = (orientation === Qt.Vertical) ? scrollBarTrack.height : scrollBarTrack.width
                var maxThumbPos = trackSize - thumbSize
                if (maxThumbPos <= 0) return
                var clickRatio = Math.max(0, Math.min((mousePos - thumbSize / 2) / maxThumbPos, 1.0))
                
                if (orientation === Qt.Vertical) {
                    var newContentY = clickRatio * (target.contentHeight - target.height)
                    target.contentY = Math.max(0, Math.min(newContentY, target.contentHeight - target.height))
                } else {
                    var newContentX = clickRatio * (target.contentWidth - target.width)
                    target.contentX = Math.max(0, Math.min(newContentX, target.contentWidth - target.width))
                }
            }
            // Track clicks also start a fresh drag at the new scroll position.
            pressedPos = mousePos
            initialContentPos = (orientation === Qt.Vertical) ? target.contentY : target.contentX
        }
        
        onPositionChanged: (mouse) => {
            if (!pressed || !target) return
            
            var mousePos = (orientation === Qt.Vertical) ? mouse.y : mouse.x
            var delta = mousePos - pressedPos
            
            var trackSize = (orientation === Qt.Vertical) ? scrollBarTrack.height : scrollBarTrack.width
            var thumbSize = (orientation === Qt.Vertical) ? scrollBarThumb.height : scrollBarThumb.width
            var maxThumbPos = trackSize - thumbSize
            
            if (maxThumbPos > 0) {
                var relativeDelta = delta / maxThumbPos
                
                if (orientation === Qt.Vertical) {
                    var maxContentY = target.contentHeight - target.height
                    var newContentY = initialContentPos + (relativeDelta * maxContentY)
                    target.contentY = Math.max(0, Math.min(newContentY, maxContentY))
                } else {
                    var maxContentX = target.contentWidth - target.width
                    var newContentX = initialContentPos + (relativeDelta * maxContentX)
                    target.contentX = Math.max(0, Math.min(newContentX, maxContentX))
                }
            }
        }
    }
}
