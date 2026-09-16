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
    property bool fullscreen: false
    property bool playlistVisible: true
    property bool alwaysOnTop: false
    property bool controlsVisible: true

    // In fullscreen the chrome gets out of the way; windowed, it always stays.
    Timer {
        id: idleTimer
        interval: 2600
        onTriggered: if (root.fullscreen) root.controlsVisible = false
    }
    onControlsVisibleChanged: if (controlsVisible && fullscreen) idleTimer.restart()

    // Collapse order as the window narrows: playlist panel first, then the volume slider
    // (inside TransportBar), then the artist line. The transport itself never collapses.
    readonly property bool roomForPlaylist: width >= 700
    readonly property bool showPlaylist: !mini && !fullscreen && playlistVisible && roomForPlaylist

    onFullscreenChanged: {
        root.visibility = fullscreen ? Window.FullScreen : Window.Windowed
        if (fullscreen) {
            controlsVisible = true
            fullscreenHint.show()
            idleTimer.restart()
        }
    }

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
        // Add or clear the single hint rather than assigning a whole flag set: replacing the
        // flags wholesale drops the title and system-menu hints with it, and the window loses
        // its decoration on some window managers.
        root.flags = alwaysOnTop ? (root.flags | Qt.WindowStaysOnTopHint)
                                 : (root.flags & ~Qt.WindowStaysOnTopHint)
    }

    function playIndex(index) {
        if (index < 0 || index >= PlaylistModel.count)
            return
        PlaylistModel.currentIndex = index
        AudioEngine.setSource(PlaylistModel.currentPath)
        AudioEngine.play()
    }

    // Pressing Play with a loaded playlist but no track chosen should just play, rather than
    // doing nothing and leaving "Nothing playing" on screen. That was a real dead end: adding
    // a file and pressing Play looked broken until you knew to double-click the row.
    function togglePlay() {
        if (AudioEngine.source === "" && PlaylistModel.count > 0) {
            playIndex(PlaylistModel.currentIndex >= 0 ? PlaylistModel.currentIndex : 0)
            return
        }
        AudioEngine.togglePlayPause()
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
                    presetsPath: initialPresetsPath
                    curatedList: initialCuratedList
                }

                // Double-click the visualiser for fullscreen, the same gesture every video
                // player uses. Moving the mouse brings the controls back.
                TapHandler {
                    onDoubleTapped: root.fullscreen = !root.fullscreen
                }
                HoverHandler {
                    id: visualizerHover
                    onPointChanged: {
                        if (!root.fullscreen)
                            return
                        if (!root.controlsVisible)
                            fullscreenHint.show()
                        root.controlsVisible = true
                        idleTimer.restart()
                    }
                }

                // Frame-time overlay, required from the first commit: over RustDesk observed
                // smoothness describes the video stream, not the application.
                Rectangle {
                    visible: overlayToggle.checked
                    anchors { left: parent.left; top: parent.top; margins: 10 }
                    width: overlayText.implicitWidth + 18
                    height: overlayText.implicitHeight + 14
                    color: Theme.overlayBackground
                    radius: 4
                    Text {
                        id: overlayText
                        anchors.centerIn: parent
                        font.family: "monospace"
                        font.pixelSize: 11
                        color: Theme.overlayText
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

                PresetBar {
                    id: presetBar
                    visualizer: visualizer
                    showQuality: true
                    anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                    anchors.bottomMargin: 14
                    opacity: root.controlsVisible ? 1.0 : 0.0
                    visible: opacity > 0.01
                    Behavior on opacity { NumberAnimation { duration: 180 } }
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
            visible: !root.fullscreen || root.controlsVisible
            compact: root.mini
            onRequestNext: root.playNext()
            onRequestPrevious: root.playPrevious()
            onRequestPlayPause: root.togglePlay()
            onRequestStop: AudioEngine.stop()
        }
    }

    // --- chrome ------------------------------------------------------------------------

    RowLayout {
        // Keep clear of the playlist panel: this chrome belongs to the visualiser area.
        anchors { right: parent.right; top: parent.top; margins: 10 }
        anchors.rightMargin: root.showPlaylist ? 350 : 10
        visible: !root.mini && (!root.fullscreen || root.controlsVisible)
        opacity: root.controlsVisible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 180 } }
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
            glyph: "expand"
            onClicked: root.fullscreen = !root.fullscreen
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Fullscreen visualiser")
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
        Menu {
            title: qsTr("Appearance")
            MenuItem {
                text: qsTr("Follow system theme")
                checkable: true
                checked: SystemTheme.preference === SystemTheme.FollowSystem
                onTriggered: SystemTheme.preference = SystemTheme.FollowSystem
            }
            MenuItem {
                text: qsTr("Always dark")
                checkable: true
                checked: SystemTheme.preference === SystemTheme.AlwaysDark
                onTriggered: SystemTheme.preference = SystemTheme.AlwaysDark
            }
            MenuItem {
                text: qsTr("Always light")
                checkable: true
                checked: SystemTheme.preference === SystemTheme.AlwaysLight
                onTriggered: SystemTheme.preference = SystemTheme.AlwaysLight
            }
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
            MenuItem {
                text: qsTr("Automatic")
                checkable: true
                checked: visualizer.adaptiveQuality
                onTriggered: visualizer.adaptiveQuality = checked
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Low (25%)")
                onTriggered: { visualizer.adaptiveQuality = false; visualizer.renderScale = 0.25 }
            }
            MenuItem {
                text: qsTr("Medium (50%)")
                onTriggered: { visualizer.adaptiveQuality = false; visualizer.renderScale = 0.5 }
            }
            MenuItem {
                text: qsTr("High (75%)")
                onTriggered: { visualizer.adaptiveQuality = false; visualizer.renderScale = 0.75 }
            }
            MenuItem {
                text: qsTr("Full (100%)")
                onTriggered: { visualizer.adaptiveQuality = false; visualizer.renderScale = 1.0 }
            }
        }
        Menu {
            title: qsTr("Presets")
            MenuItem {
                text: qsTr("Curated set")
                checkable: true
                checked: visualizer.curatedList !== ""
                onTriggered: visualizer.curatedList = checked ? initialCuratedList : ""
            }
            MenuItem {
                text: qsTr("Shuffle presets")
                checkable: true
                checked: visualizer.shuffle
                onTriggered: visualizer.shuffle = checked
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Change every 15 seconds"); onTriggered: visualizer.presetDuration = 15 }
            MenuItem { text: qsTr("Change every 30 seconds"); onTriggered: visualizer.presetDuration = 30 }
            MenuItem { text: qsTr("Change every 2 minutes");  onTriggered: visualizer.presetDuration = 120 }
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

    // Fullscreen removes the title bar, so say how to get back. Shown on entry and again
    // whenever the pointer moves after the chrome has hidden itself.
    Rectangle {
        id: fullscreenHint
        function show() { opacity = 1.0; hintTimer.restart() }
        visible: root.fullscreen && opacity > 0.01
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 24 }
        width: hintLabel.implicitWidth + 28
        height: hintLabel.implicitHeight + 18
        radius: Theme.radius
        color: Theme.overlayBackground
        Label {
            id: hintLabel
            anchors.centerIn: parent
            text: qsTr("Press Esc or double-click to leave fullscreen")
            color: Theme.overlayText
            font.pixelSize: 12
        }
        Timer { id: hintTimer; interval: 3200; onTriggered: fullscreenHint.opacity = 0 }
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

    Shortcut { sequence: "Space";       onActivated: root.togglePlay() }
    Shortcut { sequence: "Right";       onActivated: AudioEngine.seek(AudioEngine.position + 5000) }
    Shortcut { sequence: "Left";        onActivated: AudioEngine.seek(Math.max(0, AudioEngine.position - 5000)) }
    Shortcut { sequence: "Ctrl+Right";  onActivated: root.playNext() }
    Shortcut { sequence: "Ctrl+Left";   onActivated: root.playPrevious() }
    Shortcut { sequence: "Ctrl+M";      onActivated: root.mini = !root.mini }
    Shortcut { sequence: "Ctrl+L";      onActivated: root.playlistVisible = !root.playlistVisible }
    Shortcut { sequence: "Up";          onActivated: AudioEngine.volume = Math.min(1, AudioEngine.volume + 0.05) }
    Shortcut { sequence: "Down";        onActivated: AudioEngine.volume = Math.max(0, AudioEngine.volume - 0.05) }
    Shortcut { sequence: "F";           onActivated: root.fullscreen = !root.fullscreen }
    Shortcut { sequence: "N";           onActivated: visualizer.nextPreset() }
    Shortcut { sequence: "P";           onActivated: visualizer.previousPreset() }
    Shortcut { sequence: "R";           onActivated: visualizer.randomPreset() }
    Shortcut { sequence: "L";           onActivated: visualizer.presetLocked = !visualizer.presetLocked }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (root.fullscreen) root.fullscreen = false
            else if (root.mini) root.mini = false
        }
    }
}
