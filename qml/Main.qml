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

    function fitMenuWidth(menu, minimum, maximum) {
        let widest = 0
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.implicitWidth > widest)
                widest = item.implicitWidth
        }
        // Leave room for the submenu arrow and the checkable indicator column.
        menu.width = Math.max(minimum, Math.min(maximum, widest + 44))
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

    Component.onCompleted: {
        PresetLibrary.rootPath = initialPresetsPath
        PresetLibrary.curatedList = initialCuratedList
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

                // The visualiser area is black in its own right, not just when projectM
                // happens to have cleared its buffer. Covers startup, resizes and every frame
                // where there is no texture yet, in either theme.
                Rectangle {
                    anchors.fill: parent
                    color: "black"
                }

                ProjectMItem {
                    id: visualizer
                    anchors.fill: parent
                    audioEngine: AudioEngine
                    // Black unless audio is actually flowing. Pausing counts as not playing.
                    active: AudioEngine.state === AudioEngine.Playing
                            || AudioEngine.state === AudioEngine.Buffering
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
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: presetMenu.popup()
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
                    color: Theme.overlayTextDim
                    font.pixelSize: 16
                }

            }

            Rectangle {
                visible: playlistPanel.Layout.preferredWidth > 0
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            PlaylistPanel {
                id: playlistPanel
                visible: Layout.preferredWidth > 0
                clip: true
                Layout.preferredWidth: root.showPlaylist ? 340 : 0
                Layout.fillHeight: true
                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                }
                onPlayRequested: function(index) { root.playIndex(index) }
                onCollapseRequested: root.playlistVisible = false
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

    Rectangle {
        // Keep clear of the playlist panel: this chrome belongs to the visualiser area. It
        // carries its own backdrop because the preset behind it can be any colour.
        anchors { right: parent.right; top: parent.top; margins: 10 }
        // The edge handle is vertically centred, so it never reaches this corner.
        anchors.rightMargin: root.showPlaylist ? 350 : 10
        visible: !root.mini && (!root.fullscreen || root.controlsVisible)
        opacity: root.controlsVisible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        width: chromeRow.implicitWidth + 12
        height: chromeRow.implicitHeight + 8
        radius: Theme.radius
        color: Theme.overlayBackground

    RowLayout {
        id: chromeRow
        anchors.centerIn: parent
        spacing: 4

        IconButton {
            glyph: "expand"
            overVideo: true
            onClicked: root.fullscreen = !root.fullscreen
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Fullscreen visualiser")
        }
        IconButton {
            glyph: "mini"
            overVideo: true
            onClicked: root.mini = true
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Mini player")
        }
        IconButton {
            glyph: "menu"
            overVideo: true
            onClicked: optionsMenu.popup()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Options")
        }
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

    // Right-click the visualiser. Grouped by the categories the preset pack already ships
    // with, then by author where an author actually has several presets in that category.
    Menu {
        id: presetMenu
        onAboutToShow: root.fitMenuWidth(presetMenu, 200, 460)

        function useList(category) {
            visualizer.setPresetList(PresetLibrary.paths(category))
        }

        MenuItem {
            text: visualizer.presetName !== "" ? visualizer.presetName : qsTr("No visualisation")
            enabled: false
            ToolTip.visible: hovered && visualizer.presetName !== ""
            ToolTip.text: visualizer.presetName
        }
        MenuSeparator {}

        // Categories are inserted here, above this separator.
        MenuSeparator {}
        MenuItem {
            text: qsTr("Stay on this one")
            checkable: true
            checked: visualizer.presetLocked
            onTriggered: visualizer.presetLocked = checked
        }
        MenuItem {
            text: qsTr("Randomize")
            onTriggered: {
                presetMenu.useList("")
                visualizer.shuffle = true
                visualizer.presetLocked = false
                visualizer.randomPreset()
            }
        }

        Instantiator {
            model: PresetLibrary.categories
            delegate: categoryMenu
            // Index 2 keeps the header and its separator on top and pushes the trailing
            // separator, lock and Randomize down as categories arrive.
            onObjectAdded: function(index, object) { presetMenu.insertMenu(2 + index, object) }
            onObjectRemoved: function(index, object) { presetMenu.removeMenu(object) }
        }
    }

    Component {
        id: categoryMenu
        Menu {
            id: catMenu
            required property string modelData
            title: modelData
            property bool populated: false

            MenuItem {
                text: qsTr("Randomize %1").arg(catMenu.modelData)
                onTriggered: {
                    presetMenu.useList(catMenu.modelData)
                    visualizer.shuffle = true
                    visualizer.presetLocked = false
                    visualizer.randomPreset()
                }
            }
            MenuSeparator {}

            // Built on first open. Creating every menu item up front is a visible stall on a
            // software renderer and most categories are never opened.
            onAboutToShow: {
                if (populated)
                    return
                populated = true
                const category = catMenu.modelData

                const groups = PresetLibrary.artistGroups(category)
                for (let g = 0; g < groups.length; ++g) {
                    const sub = artistMenu.createObject(catMenu, {
                        title: groups[g].artist,
                        category: category,
                        entries: groups[g].items
                    })
                    catMenu.addMenu(sub)
                }

                const loose = PresetLibrary.ungrouped(category)
                for (let i = 0; i < loose.length; ++i) {
                    catMenu.addItem(presetItem.createObject(catMenu, {
                        text: loose[i].name,
                        category: category,
                        presetIndex: loose[i].index
                    }))
                }
                root.fitMenuWidth(catMenu, 200, 620)
            }
        }
    }

    Component {
        id: artistMenu
        Menu {
            id: artist
            property string category: ""
            property var entries: []
            property bool populated: false
            onAboutToShow: {
                if (populated)
                    return
                populated = true
                for (let i = 0; i < entries.length; ++i) {
                    artist.addItem(presetItem.createObject(artist, {
                        text: entries[i].name,
                        category: artist.category,
                        presetIndex: entries[i].index
                    }))
                }
                root.fitMenuWidth(artist, 180, 620)
            }
        }
    }

    Component {
        id: presetItem
        MenuItem {
            property string category: ""
            property int presetIndex: 0
            // Choosing one deliberately means staying on it; the rotation would move off it
            // in half a minute otherwise.
            onTriggered: {
                visualizer.setPresetList(PresetLibrary.paths(category))
                visualizer.jumpTo(presetIndex)
                visualizer.presetLocked = true
            }
        }
    }

    Menu {
        id: optionsMenu
        onAboutToShow: root.fitMenuWidth(optionsMenu, 220, 420)
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
            checked: false
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
                onTriggered: {
                    const list = checked ? initialCuratedList : ""
                    visualizer.curatedList = list
                    PresetLibrary.curatedList = list
                    // An explicit category selection is no longer valid across a library swap.
                    visualizer.setPresetList([])
                }
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

    // With the panel collapsed its own control is gone, so leave a handle on the edge it
    // retracted into. Hidden in mini and fullscreen, and when the window is too narrow to
    // show the panel at all.
    Rectangle {
        id: playlistHandle
        visible: !root.mini && !root.fullscreen && root.roomForPlaylist && !root.playlistVisible
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: 22
        height: 64
        radius: Theme.radius
        color: handleHover.hovered ? Theme.surfaceHigh : Theme.surface
        border.color: Theme.border
        border.width: 1
        Behavior on color { ColorAnimation { duration: 90 } }

        HoverHandler { id: handleHover }
        TapHandler { onTapped: root.playlistVisible = true }

        IconButton {
            anchors.centerIn: parent
            glyph: "chevronLeft"
            size: 20
            enabled: false          // the whole tab is the target; this is just the glyph
            opacity: 1.0
        }
        ToolTip.visible: handleHover.hovered
        ToolTip.text: qsTr("Show playlist")
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
