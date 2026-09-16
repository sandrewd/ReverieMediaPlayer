import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import Player

ApplicationWindow {
    id: root

    width: 1100
    height: 680
    visible: true
    title: PlaylistModel.currentTitle !== "" ? PlaylistModel.currentTitle + " — Player"
                                             : qsTr("Player")
    color: Theme.background

    // A real minimum size with a defined collapse order, decided up front so the mini-player
    // is a layout state rather than a later refactor of everything that assumed a big window.
    minimumWidth: mini ? 360 : Theme.minWindowWidth
    minimumHeight: mini ? 62 : Theme.minWindowHeight

    property bool mini: false
    property bool playlistVisible: true
    property bool alwaysOnTop: false

    // Collapse order as the window narrows: playlist panel first, then the volume slider
    // (inside TransportBar), then the artist line. The transport itself never collapses.
    readonly property bool roomForPlaylist: width >= 700
    readonly property bool showPlaylist: !mini && playlistVisible && roomForPlaylist

    onMiniChanged: {
        if (mini) {
            root.showNormal()
            root.width = 460
            root.height = 62
        } else {
            root.width = 1100
            root.height = 680
        }
    }

    // Always-on-top works through window flags on X11 and is simply not possible on Wayland:
    // there is no portable client-side protocol for it. Documented, not chased.
    onAlwaysOnTopChanged: {
        root.flags = alwaysOnTop ? (Qt.Window | Qt.WindowStaysOnTopHint) : Qt.Window
    }

    function playIndex(index) {
        if (index < 0 || index >= PlaylistModel.count)
            return
        PlaylistModel.currentIndex = index
        AudioEngine.setSource(PlaylistModel.currentPath)
        AudioEngine.play()
    }

    function playNext() { playIndex(PlaylistModel.nextIndex(true)) }
    function playPrevious() {
        // Restart the current track if the user is more than a few seconds in. Standard
        // behaviour everywhere, and it stops a mis-press losing your place in a long track.
        if (AudioEngine.position > 3000 && AudioEngine.seekable)
            AudioEngine.seek(0)
        else
            playIndex(PlaylistModel.previousIndex(true))
    }

    Connections {
        target: AudioEngine
        function onEndOfStream() { root.playNext() }
        function onErrorOccurred(message) {
            errorBanner.text = message
            errorBanner.visible = true
            errorTimer.restart()
        }
    }

    // --- main layout -------------------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.mini
            spacing: 0

            // The visualiser is the default Now Playing state, not a mode to discover.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ProjectMItem {
                    id: visualizer
                    anchors.fill: parent
                    audioEngine: AudioEngine
                    renderScale: initialScale
                    presetPath: initialPreset
                }

                // Frame-time overlay, required from the first commit: over RustDesk observed
                // smoothness describes the video stream, not the application.
                Rectangle {
                    visible: overlayToggle.checked
                    anchors { left: parent.left; top: parent.top; margins: 10 }
                    width: overlayText.implicitWidth + 18
                    height: overlayText.implicitHeight + 14
                    color: "#c0000000"
                    radius: 4
                    Text {
                        id: overlayText
                        anchors.centerIn: parent
                        font.family: "monospace"
                        font.pixelSize: 11
                        color: "#e8e8e8"
                        text: "projectM   %1 ms\nthroughput %2 fps\nrender     %3x%4 (%5%)"
                            .arg(visualizer.frameTimeMs.toFixed(2))
                            .arg(visualizer.fps.toFixed(1))
                            .arg(visualizer.renderSize.width)
                            .arg(visualizer.renderSize.height)
                            .arg(Math.round(visualizer.renderScale * 100))
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: PlaylistModel.count === 0
                    text: qsTr("Drop music here")
                    color: Theme.textDim
                    font.pixelSize: 16
                }
            }

            Rectangle {
                visible: root.showPlaylist
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            PlaylistPanel {
                visible: root.showPlaylist
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                onPlayRequested: function(index) { root.playIndex(index) }
            }
        }

        TransportBar {
            Layout.fillWidth: true
            compact: root.mini
            onRequestNext: root.playNext()
            onRequestPrevious: root.playPrevious()
        }
    }

    // --- chrome ------------------------------------------------------------------------

    RowLayout {
        // Keep clear of the playlist panel: this chrome belongs to the visualiser area.
        anchors { right: parent.right; top: parent.top; margins: 10 }
        anchors.rightMargin: root.showPlaylist ? 350 : 10
        visible: !root.mini
        spacing: 4

        IconButton {
            glyph: "list"
            enabled: root.roomForPlaylist
            onClicked: root.playlistVisible = !root.playlistVisible
            ToolTip.visible: hovered
            ToolTip.text: root.roomForPlaylist ? qsTr("Show or hide the playlist")
                                               : qsTr("Window too narrow for the playlist")
        }
        IconButton {
            glyph: "mini"
            onClicked: root.mini = true
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Mini player")
        }
        IconButton {
            glyph: "menu"
            onClicked: optionsMenu.popup()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Options")
        }
    }

    IconButton {
        anchors { right: parent.right; top: parent.top; margins: 6 }
        visible: root.mini
        glyph: "mini"
        size: 26
        onClicked: root.mini = false
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Back to full player")
    }

    Menu {
        id: optionsMenu
        MenuItem {
            text: qsTr("Save playlist as M3U…")
            enabled: PlaylistModel.count > 0
            onTriggered: saveDialog.open()
        }
        MenuItem {
            text: qsTr("Load playlist…")
            onTriggered: loadDialog.open()
        }
        MenuSeparator {}
        MenuItem {
            id: persistToggle
            text: qsTr("Remember playlist between launches")
            checkable: true
            checked: PlaylistModel.persistAcrossLaunches
            onTriggered: PlaylistModel.persistAcrossLaunches = checked
        }
        MenuItem {
            text: qsTr("Keep window on top")
            checkable: true
            checked: root.alwaysOnTop
            onTriggered: root.alwaysOnTop = checked
        }
        MenuItem {
            id: overlayToggle
            text: qsTr("Show frame-time overlay")
            checkable: true
            checked: true
        }
        MenuSeparator {}
        Menu {
            title: qsTr("Visual quality")
            MenuItem { text: qsTr("Low (25%)");    onTriggered: visualizer.renderScale = 0.25 }
            MenuItem { text: qsTr("Medium (50%)"); onTriggered: visualizer.renderScale = 0.5 }
            MenuItem { text: qsTr("High (75%)");   onTriggered: visualizer.renderScale = 0.75 }
            MenuItem { text: qsTr("Full (100%)");  onTriggered: visualizer.renderScale = 1.0 }
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save playlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "m3u"
        nameFilters: [qsTr("M3U playlists (*.m3u *.m3u8)")]
        onAccepted: PlaylistModel.saveM3U(selectedFile)
    }

    FileDialog {
        id: loadDialog
        title: qsTr("Load playlist")
        nameFilters: [qsTr("M3U playlists (*.m3u *.m3u8)"), qsTr("All files (*)")]
        onAccepted: PlaylistModel.loadM3U(selectedFile)
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (!drop.hasUrls)
                return
            const files = []
            for (const url of drop.urls) {
                // A dropped folder and a dropped file look the same here; the model sorts it out.
                if (url.toString().endsWith("/"))
                    PlaylistModel.addFolder(url)
                else
                    files.push(url)
            }
            if (files.length > 0)
                PlaylistModel.addFiles(files)
        }
    }

    Rectangle {
        id: errorBanner
        property alias text: errorLabel.text
        visible: false
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        anchors.bottomMargin: 96
        height: errorLabel.implicitHeight + 20
        color: "#aa3b2a"
        Label {
            id: errorLabel
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
        }
        Timer {
            id: errorTimer
            interval: 6000
            onTriggered: errorBanner.visible = false
        }
    }

    // --- keyboard ----------------------------------------------------------------------

    Shortcut { sequence: "Space";       onActivated: AudioEngine.togglePlayPause() }
    Shortcut { sequence: "Right";       onActivated: AudioEngine.seek(AudioEngine.position + 5000) }
    Shortcut { sequence: "Left";        onActivated: AudioEngine.seek(Math.max(0, AudioEngine.position - 5000)) }
    Shortcut { sequence: "Ctrl+Right";  onActivated: root.playNext() }
    Shortcut { sequence: "Ctrl+Left";   onActivated: root.playPrevious() }
    Shortcut { sequence: "Ctrl+M";      onActivated: root.mini = !root.mini }
    Shortcut { sequence: "Ctrl+L";      onActivated: root.playlistVisible = !root.playlistVisible }
    Shortcut { sequence: "Up";          onActivated: AudioEngine.volume = Math.min(1, AudioEngine.volume + 0.05) }
    Shortcut { sequence: "Down";        onActivated: AudioEngine.volume = Math.max(0, AudioEngine.volume - 0.05) }
    Shortcut { sequence: "Escape";      onActivated: if (root.mini) root.mini = false }
}
