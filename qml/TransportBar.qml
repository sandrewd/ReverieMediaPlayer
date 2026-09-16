import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Player

// Built as one self-contained component from the first commit, deliberately: the mini-player
// reuses it verbatim, and that only stays true if it never reaches outside itself for state.
// `compact` drops the things that do not fit in a small window, in a defined order.
Rectangle {
    id: root

    property bool compact: false
    signal requestNext()
    signal requestPrevious()

    color: Theme.surface
    implicitHeight: compact ? 62 : 88

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        anchors.topMargin: compact ? 6 : Theme.spacing
        spacing: compact ? 2 : 6

        // Seek row. In compact mode the times move under the controls to save a line.
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Label {
                text: root.formatTime(AudioEngine.position)
                color: Theme.textDim
                font.family: "monospace"
                font.pixelSize: 11
                Layout.preferredWidth: 42
                horizontalAlignment: Text.AlignRight
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                enabled: AudioEngine.seekable && AudioEngine.duration > 0
                from: 0
                to: Math.max(1, AudioEngine.duration)
                // Do not fight the user's drag with position updates arriving every 100ms.
                value: pressed ? value : AudioEngine.position
                onMoved: AudioEngine.seek(value)

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.availableWidth
                    height: 4
                    radius: 2
                    color: Theme.surfaceHigh
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: seekSlider.enabled ? Theme.accent : Theme.border
                    }
                }
                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: 14; height: 14; radius: 7
                    color: Theme.accent
                    visible: seekSlider.enabled
                    scale: seekSlider.pressed ? 1.25 : (seekSlider.hovered ? 1.1 : 1.0)
                    Behavior on scale { NumberAnimation { duration: 90 } }
                }
            }

            Label {
                text: root.formatTime(AudioEngine.duration)
                color: Theme.textDim
                font.family: "monospace"
                font.pixelSize: 11
                Layout.preferredWidth: 42
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            IconButton {
                glyph: "prev"
                onClicked: root.requestPrevious()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous")
            }
            IconButton {
                glyph: AudioEngine.state === AudioEngine.Playing ? "pause" : "play"
                primary: true
                onClicked: AudioEngine.togglePlayPause()
            }
            IconButton {
                glyph: "next"
                onClicked: root.requestNext()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next")
            }

            // Track identity sits with the transport so the mini-player gets it for free.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: AudioEngine.streamTitle !== ""
                        ? AudioEngine.streamTitle
                        : (PlaylistModel.currentTitle !== "" ? PlaylistModel.currentTitle
                                                             : qsTr("Nothing playing"))
                    color: Theme.text
                    font.pixelSize: root.compact ? 12 : 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: !root.compact && PlaylistModel.currentArtist !== ""
                    text: PlaylistModel.currentArtist
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            IconButton {
                glyph: AudioEngine.muted ? "muted" : "volume"
                onClicked: AudioEngine.muted = !AudioEngine.muted
            }
            Slider {
                id: volumeSlider
                // First thing to go when the window gets narrow: the mute button still works.
                visible: !root.compact
                Layout.preferredWidth: 90
                from: 0; to: 1
                value: AudioEngine.volume
                onMoved: { AudioEngine.volume = value; AudioEngine.muted = false }

                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth; height: 4; radius: 2
                    color: Theme.surfaceHigh
                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height; radius: 2
                        color: AudioEngine.muted ? Theme.border : Theme.accent
                    }
                }
                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: 12; height: 12; radius: 6
                    color: Theme.accent
                }
            }
        }
    }

    function formatTime(ms) {
        if (!ms || ms <= 0)
            return "0:00"
        const total = Math.floor(ms / 1000)
        const m = Math.floor(total / 60)
        const s = total % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }
}
