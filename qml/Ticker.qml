import QtQuick
import QtQuick.Controls

// A single line of text that bounces left and right when it does not fit, rather than eliding it
// away or scrolling in one direction. A one-way scroll keeps the start of a title off screen most
// of the time, which is the part you actually need; bouncing always returns to it.
//
// Nothing moves unless the text genuinely overflows, so a short title is simply a static label.
Item {
    id: root

    property alias text: label.text
    property alias color: label.color
    property alias font: label.font
    // Shown while the pointer is over the line. The transport uses it for the filename, which is
    // what the tags were derived from and the only way back to it once tags replace the name.
    property string hoverText: ""

    implicitHeight: label.implicitHeight
    implicitWidth: label.implicitWidth
    clip: true

    readonly property real overflow: Math.max(0, label.implicitWidth - width)
    // Roughly constant speed rather than constant duration, so a very long title does not race.
    readonly property int travel: Math.max(700, Math.round(overflow * 22))

    Text {
        id: label
        x: 0
        y: 0
        width: Math.max(implicitWidth, root.width)
        // Deliberately no elide: the bounce is what handles overflow, and eliding would hide the
        // end of the text the animation exists to reveal.
        maximumLineCount: 1
    }

    SequentialAnimation {
        id: bounce
        running: root.overflow > 0 && root.visible && root.width > 0
        loops: Animation.Infinite

        // Long enough to read the start before it moves.
        PauseAnimation { duration: 1800 }
        NumberAnimation {
            target: label; property: "x"
            from: 0; to: -root.overflow
            duration: root.travel
            easing.type: Easing.InOutQuad
        }
        PauseAnimation { duration: 1400 }
        NumberAnimation {
            target: label; property: "x"
            from: -root.overflow; to: 0
            duration: root.travel
            easing.type: Easing.InOutQuad
        }

        // Leaving x where the animation stopped would strand a short title off to the left the
        // moment the window is widened or the track changes.
        onRunningChanged: if (!running) label.x = 0
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: root.hoverText !== ""
        acceptedButtons: Qt.NoButton
        ToolTip.visible: containsMouse && root.hoverText !== ""
        ToolTip.text: root.hoverText
        ToolTip.delay: 400
    }
}
