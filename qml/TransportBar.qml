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
    signal requestPlayPause()
    signal requestStop()
    signal requestRestore()

    // In the mini player the way back belongs in the bar, not floating over it: a loose
    // button crowds the volume control and the duration label at narrow widths.
    property bool showRestore: false

    // Floating over the stage rather than sitting under it. Neither a uniformly translucent
    // panel nor bare controls work here: the frame behind is arbitrary, so any fixed alpha is
    // readable over one shot and illegible over the next. A scrim that is near-opaque where the
    // controls are and fades to nothing above them gives contrast exactly where it is needed
    // and removes the hard edge that makes the picture look cut off.
    property bool overVideo: false

    // Text and track colours follow the backdrop, not the theme: over video the shell's own
    // colours are unusable in either palette. Same rule as IconButton.overVideo.
    readonly property color barText:    overVideo ? Theme.overlayText : Theme.text
    readonly property color barTextDim: overVideo ? Theme.overlayTextDim : Theme.textDim
    readonly property color barTrack:   overVideo ? Qt.rgba(1, 1, 1, 0.30) : Theme.surfaceHigh

    color: overVideo ? "transparent" : Theme.surface
    implicitHeight: compact ? 62 : 88

    // Taller than the bar and anchored to its bottom, so the fade starts above the controls and
    // over the picture. Nothing clips it: the gradient is the whole point.
    Rectangle {
        visible: root.overVideo
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: root.height * 2
        gradient: Gradient {
            GradientStop { position: 0.0;  color: "#00000000" }
            GradientStop { position: 0.45; color: "#66000000" }
            GradientStop { position: 1.0;  color: "#e0000000" }
        }
    }

    Rectangle {
        visible: !root.overVideo
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Theme.stageEdge
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
                color: root.barTextDim
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
                    color: root.barTrack
                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: seekSlider.enabled ? Theme.accent : root.barTrack
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
                text: PlaylistModel.currentIsStream ? qsTr("live")
                                                    : root.formatTime(AudioEngine.duration)
                color: root.barTextDim
                font.family: "monospace"
                font.pixelSize: 11
                Layout.preferredWidth: 42
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            IconButton {
                overVideo: root.overVideo
                glyph: "prev"
                onClicked: root.requestPrevious()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous")
            }
            IconButton {
                overVideo: root.overVideo
                glyph: AudioEngine.state === AudioEngine.Playing ? "pause" : "play"
                primary: true
                onClicked: root.requestPlayPause()
            }
            IconButton {
                overVideo: root.overVideo
                glyph: "stop"
                enabled: AudioEngine.state !== AudioEngine.Stopped
                onClicked: root.requestStop()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Stop")
            }
            IconButton {
                overVideo: root.overVideo
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
                    // A local file also emits a title tag from its ID3 data, but for those
                    // TagLib is authoritative. Only a stream has nothing better to offer.
                    text: PlaylistModel.currentIsStream && AudioEngine.streamTitle !== ""
                        ? AudioEngine.streamTitle
                        : (PlaylistModel.currentTitle !== "" ? PlaylistModel.currentTitle
                                                             : qsTr("Nothing playing"))
                    color: root.barText
                    font.pixelSize: root.compact ? 12 : 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: !root.compact && text !== ""
                    text: AudioEngine.buffering
                          ? qsTr("Buffering…")
                          : PlaylistModel.currentIsStream
                            ? (AudioEngine.streamStation !== "" ? AudioEngine.streamStation
                                                                : PlaylistModel.currentTitle)
                            : PlaylistModel.currentArtist
                    color: root.barTextDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            IconButton {
                overVideo: root.overVideo
                glyph: AudioEngine.muted ? "muted" : "volume"
                onClicked: AudioEngine.muted = !AudioEngine.muted
            }
            IconButton {
                overVideo: root.overVideo
                glyph: "mini"
                size: 26
                visible: root.showRestore
                onClicked: root.requestRestore()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to full player")
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
                    color: root.barTrack
                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height; radius: 2
                        color: AudioEngine.muted ? root.barTrack : Theme.accent
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
