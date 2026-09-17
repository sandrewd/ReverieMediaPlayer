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
    title: PlaylistModel.currentTitle !== ""
           ? PlaylistModel.currentTitle + " — Reverie"
           : qsTr("Reverie Media Player")
    color: Theme.background

    // Bind the window's palette to the tokens. Qt Quick Controls paint from the palette, and
    // an item created before QGuiApplication::setPalette() keeps its resolved palette - so a
    // theme changed at runtime reached the things we style by hand but left stock controls
    // (buttons, checkboxes, scrollbars) on the old colours. A window palette propagates down
    // the item tree and updates them live.
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.alternateBase: Theme.surfaceHigh
    palette.text: Theme.text
    palette.button: Theme.surfaceHigh
    palette.buttonText: Theme.text
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.light: Theme.surfaceHigh
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.dark ? "#0f1115" : "#ffffff"
    palette.placeholderText: Theme.textDim
    palette.toolTipBase: Theme.surfaceHigh
    palette.toolTipText: Theme.text

    // A real minimum size with a defined collapse order, decided up front so the mini-player
    // is a layout state rather than a later refactor of everything that assumed a big window.
    // The mini player is exactly as tall as its transport bar. Letting it be dragged taller
    // only produces empty space around the controls, so height is pinned to the content and
    // only width stays adjustable - width is the one dimension that buys anything, since a
    // longer track title has somewhere to go.
    readonly property int miniHeight: transportBar.implicitHeight
    // Pinned to the transport's height only once the window has actually taken that size. Driving
    // these straight from `mini` changed the limits in the same pass as the height, and on Wayland
    // a resize is a request the compositor confirms later - so the request was clamped against
    // limits that had not settled and the window kept its old size with the controls laid out for
    // the new one. On X11 the resize is synchronous and it happened to work, which is exactly why
    // this was invisible here for so long.
    property bool heightLocked: false
    minimumWidth: mini ? 320 : Theme.minWindowWidth
    minimumHeight: heightLocked ? miniHeight : Theme.minWindowHeight
    maximumHeight: heightLocked ? miniHeight : 16777215

    property bool mini: false
    property bool fullscreen: false
    property bool playlistVisible: true
    property bool alwaysOnTop: false

    // Our renditions in these four roles, not reproductions of anyone's published scheme.
    readonly property var curatedPalettes: [
        { name: qsTr("Midnight"),      bg: "#0f1115", sf: "#171a21", ac: "#4da3ff", tx: "#e8eaed", st: "#05070a" },
        { name: qsTr("Daylight"),      bg: "#f2f3f5", sf: "#ffffff", ac: "#1f6feb", tx: "#1b1e23", st: "#1b1e23" },
        { name: qsTr("Pastel"),        bg: "#fbf1f6", sf: "#ffffff", ac: "#e39ec1", tx: "#4a3b46", st: "#3a2f36" },
        { name: qsTr("Funky"),         bg: "#1b1035", sf: "#2a1a52", ac: "#ff5fa2", tx: "#ffe7f4", st: "#12082a" },
        { name: qsTr("Techno"),        bg: "#05010a", sf: "#12032a", ac: "#00ffd5", tx: "#d7f9ff", st: "#02000a" },
        { name: qsTr("High contrast"), bg: "#000000", sf: "#0b0b0b", ac: "#ffff00", tx: "#ffffff", st: "#000000" },
        { name: qsTr("Forest"),        bg: "#10201a", sf: "#17322a", ac: "#6ee7a8", tx: "#e2f5ea", st: "#081410" },
        { name: qsTr("Ember"),         bg: "#1d1210", sf: "#2e1d19", ac: "#ff7a45", tx: "#ffe9df", st: "#140b09" },
        { name: qsTr("Slate"),         bg: "#2e3440", sf: "#3b4252", ac: "#88c0d0", tx: "#eceff4", st: "#232831" },
        { name: qsTr("Sepia"),         bg: "#f4ecd8", sf: "#fffaf0", ac: "#a2673f", tx: "#3b2f2a", st: "#2a221c" }
    ]
    property bool controlsVisible: true

    // In fullscreen the chrome gets out of the way; windowed, it always stays.
    Timer {
        id: idleTimer
        interval: 2600
        onTriggered: if (root.fullscreen) root.controlsVisible = false
    }
    onControlsVisibleChanged: if (controlsVisible && fullscreen) idleTimer.restart()

    // Any sign of life brings the chrome back and resets the countdown.
    function revealControls() {
        if (!root.fullscreen)
            return
        if (!root.controlsVisible)
            fullscreenHint.show()
        root.controlsVisible = true
        idleTimer.restart()
    }

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
        // Deferred a pass on purpose: `miniHeight` follows the transport bar's implicit height,
        // which follows `compact`, which follows `mini` - so at this instant it still reports the
        // height of the mode we are leaving.
        Qt.callLater(applyModeSize)
    }

    // The limits move before the size, and in the direction that admits it. Getting this
    // backwards is what broke the mini player: the height was set while minimumHeight was still
    // the full window's 320, so a request for 62 was clamped straight back up to 320 and the
    // window sat there with the controls at the bottom of a mostly empty pane. X11 hid it because
    // a later resize re-clamped against the settled limits; Wayland has no such second chance.
    function applyModeSize() {
        if (mini) {
            root.showNormal()
            root.heightLocked = true
            root.width = Math.max(320, 460)
            root.height = root.miniHeight
        } else {
            root.heightLocked = false
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

    // Editing a colour while some other theme is active looked like nothing happening, so
    // choosing one switches to the custom theme rather than quietly storing it for later.
    function applyCustomColour(role, picked) {
        switch (role) {
        case "background": SystemTheme.customBackground = picked; break
        case "surface":    SystemTheme.customSurface = picked; break
        case "accent":     SystemTheme.customAccent = picked; break
        case "text":       SystemTheme.customText = picked; break
        case "stage":      SystemTheme.customStage = picked; break
        }
        SystemTheme.rememberColour(picked)
        if (SystemTheme.preference !== SystemTheme.Custom)
            SystemTheme.preference = SystemTheme.Custom
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
        // Resume only when the engine's source is still the track the playlist is pointing
        // at. If nothing is current - the list was cleared, or the playing track was removed -
        // the loaded source is an orphan, and resuming it would play audio with no row marked
        // to say what it is. Start the list instead.
        const nothingCurrent = PlaylistModel.currentIndex < 0
        if ((AudioEngine.source === "" || nothingCurrent) && PlaylistModel.count > 0) {
            playIndex(nothingCurrent ? 0 : PlaylistModel.currentIndex)
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

    // One invariant, enforced in one place: if no row is current, nothing should be playing.
    // Clearing the playlist and removing the playing track both land here, and both used to
    // leave audio running with nothing in the list to show for it.
    Connections {
        target: PlaylistModel
        function onCurrentIndexChanged() {
            if (PlaylistModel.currentIndex < 0 && AudioEngine.state !== AudioEngine.Stopped)
                AudioEngine.stop()
        }
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
                    // Black behind video, because letterbox bars should not be tinted; the
                    // themed stage colour otherwise, so the window has an edge on a dark desktop.
                    color: AudioEngine.hasVideo ? "black" : Theme.stage
                }

                // Video takes the stage when the media has any; the visualiser is what an
                // audio-only track gets. Both live in the same slot, so fullscreen, the
                // collapsing playlist and the floating transport all work unchanged.
                VideoSurface {
                    id: videoSurface
                    anchors.fill: parent
                    visible: AudioEngine.hasVideo
                }

                ProjectMItem {
                    id: visualizer
                    anchors.fill: parent
                    visible: !AudioEngine.hasVideo
                    audioEngine: AudioEngine
                    // Black unless audio is actually flowing. Pausing counts as not playing;
                    // buffering does not, since the stream is still playing while it tops up.
                    // Idle whenever video is on screen: rendering presets behind an opaque
                    // video surface is pure waste, and on a software renderer it is waste the
                    // video decode needs.
                    active: AudioEngine.state === AudioEngine.Playing && !AudioEngine.hasVideo
                    // Video shares this render thread and has presentation deadlines and A/V
                    // sync to hold, so it must not be scheduled as a batch workload. The
                    // visualiser has neither and should keep yielding. See the brief, 9c.
                    yieldToDesktop: !AudioEngine.hasVideo
                    maxFps: initialMaxFps
                    // No binding here: the item loads the user's stored quality in its
                    // constructor, and a binding would overwrite it on every startup.
                    Component.onCompleted: if (initialScale > 0) renderScale = initialScale
                    presetPath: initialPreset
                    presetsPath: initialPresetsPath
                    curatedList: initialCuratedList
                }

                // Double-click the visualiser for fullscreen, the same gesture every video
                // player uses. Moving the mouse brings the controls back.
                TapHandler {
                    // Guarded for the same reason: with button filtering skipped, a right-click
                    // double-tap would otherwise toggle fullscreen as well as open the picker.
                    onDoubleTapped: function(point, button) {
                        if (button === Qt.LeftButton || button === Qt.NoButton)
                            root.fullscreen = !root.fullscreen
                    }
                }
                // The button is checked in the handler, not left to `acceptedButtons`, because
                // that filter is not always applied. Qt only consults device type, pointer type
                // and modifiers when deciding whether a handler wants an event; button filtering
                // is skipped for devices it considers touch-like. Under Wayland an ordinary USB
                // mouse can be reported as QPointingDevice("touchpad" TouchPad ptrType=Finger),
                // and then this handler accepted *left* clicks - so clicking anything over the
                // visualiser, including the chrome buttons, also opened the preset picker.
                //
                // Diagnosed from qt.quick.handler logging on the affected machine: both this
                // handler and the double-tap one below reported WANTS on a MouseButtonPress
                // LeftButton and each took a passive grab.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    // Nothing in this menu applies to video: there is no preset on screen to
                    // change, lock or randomise.
                    onTapped: function(point, button) {
                        if (button !== Qt.RightButton)
                            return
                        if (!AudioEngine.hasVideo)
                            presetMenu.popup()
                    }
                }
                // Touch has no second button, so the picker gets the gesture touch actually uses
                // for a context menu.
                TapHandler {
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: if (!AudioEngine.hasVideo) presetMenu.popup()
                }
                // A MouseArea reports genuine movement; HoverHandler's point also changes on
                // scene updates, which kept restarting the idle timer every frame and meant
                // the controls never actually hid. Buttons are not accepted, so clicks still
                // reach the handlers underneath.
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    cursorShape: root.fullscreen && !root.controlsVisible ? Qt.BlankCursor
                                                                          : Qt.ArrowCursor
                    onPositionChanged: root.revealControls()
                }

                // Frame-time overlay, required from the first commit: over a remote desktop the observed
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

                ColumnLayout {
                    anchors.centerIn: parent
                    visible: PlaylistModel.count === 0
                    spacing: 14
                    ReverieMark {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 96
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Drop music here")
                        color: Theme.overlayTextDim
                        font.pixelSize: 16
                    }
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
                onAddStreamRequested: streamDialog.open()
            }
        }

        // In fullscreen the transport floats over the visualiser instead of taking a slice of
        // the window, so the visualisation really does fill the screen. Windowed, this spacer
        // reserves exactly the height the bar occupies below.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.fullscreen ? 0 : transportBar.implicitHeight
        }
    }

    TransportBar {
        id: transportBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        compact: root.mini
        showRestore: root.mini
        // Fullscreen is the only state where the bar is over the picture rather than below it.
        overVideo: root.fullscreen
        opacity: root.fullscreen && !root.controlsVisible ? 0.0 : 1.0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 200 } }
        onRequestRestore: root.mini = false
        onRequestNext: root.playNext()
        onRequestPrevious: root.playPrevious()
        onRequestPlayPause: root.togglePlay()
        onRequestStop: AudioEngine.stop()
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
            ToolTip.text: AudioEngine.hasVideo ? qsTr("Fullscreen video") : qsTr("Fullscreen visualiser")
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
            text: qsTr("Add a radio stream…")
            onTriggered: streamDialog.open()
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
            checked: false
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Visual quality…")
            onTriggered: qualityDialog.open()
        }
        Menu {
            // Was "Presets", which said nothing about what it governed and collided
            // head-on with the equaliser's presets once those existed.
            id: visualisationMenu
            title: qsTr("Visualisation")
            onAboutToShow: root.fitMenuWidth(visualisationMenu, 220, 420)
            MenuItem {
                text: qsTr("Use the curated selection")
                // A package that ships only the curated presets installs no list, and then this
                // switches between a set and itself. Hide it rather than have it do nothing.
                visible: initialCuratedList !== ""
                height: visible ? implicitHeight : 0
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
                text: qsTr("Shuffle the order")
                checkable: true
                checked: visualizer.shuffle
                onTriggered: visualizer.shuffle = checked
            }
            MenuSeparator {}

            // These were four plain items with no checked state, so nothing showed which one was
            // in force, and there was no way to stop the rotation at all - the only way to hold a
            // visualisation was to right-click it and pick one. Exclusive now, with holding as a
            // first-class choice.
            ButtonGroup { id: visualisationIntervalGroup }

            MenuItem {
                text: qsTr("Stay on one visualisation")
                checkable: true
                ButtonGroup.group: visualisationIntervalGroup
                checked: visualizer.presetLocked
                onTriggered: visualizer.presetLocked = true
            }
            MenuItem {
                text: qsTr("Change every 15 seconds")
                checkable: true
                ButtonGroup.group: visualisationIntervalGroup
                checked: !visualizer.presetLocked && visualizer.presetDuration === 15
                onTriggered: { visualizer.presetLocked = false; visualizer.presetDuration = 15 }
            }
            MenuItem {
                text: qsTr("Change every 30 seconds")
                checkable: true
                ButtonGroup.group: visualisationIntervalGroup
                checked: !visualizer.presetLocked && visualizer.presetDuration === 30
                onTriggered: { visualizer.presetLocked = false; visualizer.presetDuration = 30 }
            }
            MenuItem {
                text: qsTr("Change every 2 minutes")
                checkable: true
                ButtonGroup.group: visualisationIntervalGroup
                checked: !visualizer.presetLocked && visualizer.presetDuration === 120
                onTriggered: { visualizer.presetLocked = false; visualizer.presetDuration = 120 }
            }
        }

        Menu {
            title: qsTr("Appearance")

            // A ButtonGroup makes these genuinely exclusive: clicking the active one cannot
            // leave the group with nothing selected, which is what happened when they were
            // four independent checkboxes. It also stops QML breaking the `checked` binding
            // on click - assigning to checked detaches it, and setting the preference to the
            // value it already held emitted no change to restore it.
            ButtonGroup { id: appearanceGroup }

            MenuItem {
                text: qsTr("Follow system theme")
                checkable: true
                ButtonGroup.group: appearanceGroup
                checked: SystemTheme.preference === SystemTheme.FollowSystem
                onTriggered: SystemTheme.preference = SystemTheme.FollowSystem
            }
            MenuItem {
                text: qsTr("Always dark")
                checkable: true
                ButtonGroup.group: appearanceGroup
                checked: SystemTheme.preference === SystemTheme.AlwaysDark
                onTriggered: SystemTheme.preference = SystemTheme.AlwaysDark
            }
            MenuItem {
                text: qsTr("Always light")
                checkable: true
                ButtonGroup.group: appearanceGroup
                checked: SystemTheme.preference === SystemTheme.AlwaysLight
                onTriggered: SystemTheme.preference = SystemTheme.AlwaysLight
            }
            MenuItem {
                text: qsTr("My own colours")
                checkable: true
                ButtonGroup.group: appearanceGroup
                checked: SystemTheme.preference === SystemTheme.Custom
                onTriggered: {
                    SystemTheme.preference = SystemTheme.Custom
                    themeDialog.open()
                }
            }
            MenuSeparator {}

            // Palettes live here rather than inside the colour editor: choosing "my own
            // colours" and then picking somebody else's curated set read as a contradiction.
            Menu {
                id: palettesMenu
                title: qsTr("Palettes")

                Instantiator {
                    model: root.curatedPalettes
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: SystemTheme.applyPalettePreset(modelData.bg, modelData.sf,
                                                                    modelData.ac, modelData.tx,
                                                                    modelData.st)
                    }
                    onObjectAdded: function(index, object) { palettesMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { palettesMenu.removeItem(object) }
                }

                MenuSeparator {
                    // Only worth a divider once there is something below it.
                    visible: SystemTheme.savedPalettes.length > 0
                    height: visible ? implicitHeight : 0
                }

                Instantiator {
                    model: SystemTheme.savedPalettes
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: SystemTheme.applySavedPalette(modelData.name)
                    }
                    onObjectAdded: function(index, object) {
                        palettesMenu.insertItem(root.curatedPalettes.length + 1 + index, object)
                    }
                    onObjectRemoved: function(index, object) { palettesMenu.removeItem(object) }
                }

                MenuSeparator {}
                MenuItem {
                    text: qsTr("Edit and save colours…")
                    onTriggered: themeDialog.open()
                }
            }
        }

        Menu {
            id: soundMenu
            title: qsTr("Sound")
            onAboutToShow: root.fitMenuWidth(soundMenu, 220, 420)

            // Exclusive for the same reason the appearance options are: four independent
            // checkboxes can be left with nothing selected, because clicking the active one
            // assigns checked = false and detaches the binding.
            ButtonGroup { id: equaliserGroup }

            MenuItem {
                text: qsTr("No equaliser")
                checkable: true
                ButtonGroup.group: equaliserGroup
                checked: !AudioEngine.equaliserEnabled
                onTriggered: AudioEngine.chooseNoEqualiser()
            }
            MenuSeparator {}
            // Instantiator, not Repeater: a Menu builds its items through insertItem, and a
            // Repeater tries to assign them as visual children, which a Menu has no property for.
            Instantiator {
                model: AudioEngine.equaliserPresetNames
                delegate: MenuItem {
                    text: modelData
                    checkable: true
                    ButtonGroup.group: equaliserGroup
                    checked: AudioEngine.equaliserEnabled
                             && AudioEngine.equaliserPreset === modelData
                    onTriggered: AudioEngine.applyEqualiserPreset(modelData)
                }
                // After "No equaliser" and its separator.
                onObjectAdded: function(index, object) { soundMenu.insertItem(2 + index, object) }
                onObjectRemoved: function(index, object) { soundMenu.removeItem(object) }
            }
            MenuSeparator {}
            MenuItem {
                // Shown as selected when the bands have been edited by hand, which is what an
                // empty preset name means.
                text: qsTr("Custom…")
                checkable: true
                ButtonGroup.group: equaliserGroup
                checked: AudioEngine.equaliserEnabled && AudioEngine.equaliserPreset === ""
                onTriggered: { AudioEngine.applyCustomEqualiser(); equaliserDialog.open() }
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("About…")
            onTriggered: aboutDialog.open()
        }
    }

    Dialog {
        id: themeDialog
        title: qsTr("Colours")
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - 60)
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Pick five colours and the rest is worked out from them, so nothing ends up unreadable. Visualiser is the area behind the artwork.")
                color: Theme.textDim
                font.pixelSize: 11
            }

            // Written out one per role rather than generated from a list of property names:
            // a dynamic lookup like SystemTheme[key] does not register as a binding
            // dependency, so those rows never updated when the value changed.
            ColourRow {
                Layout.fillWidth: true
                label: qsTr("Background")
                colour: SystemTheme.customBackground
                onColourPicked: function(picked) { root.applyCustomColour("background", picked) }
            }
            ColourRow {
                Layout.fillWidth: true
                label: qsTr("Panels")
                colour: SystemTheme.customSurface
                onColourPicked: function(picked) { root.applyCustomColour("surface", picked) }
            }
            ColourRow {
                Layout.fillWidth: true
                label: qsTr("Highlight")
                colour: SystemTheme.customAccent
                onColourPicked: function(picked) { root.applyCustomColour("accent", picked) }
            }
            ColourRow {
                Layout.fillWidth: true
                label: qsTr("Text")
                colour: SystemTheme.customText
                onColourPicked: function(picked) { root.applyCustomColour("text", picked) }
            }
            ColourRow {
                Layout.fillWidth: true
                label: qsTr("Visualiser")
                colour: SystemTheme.customStage
                onColourPicked: function(picked) { root.applyCustomColour("stage", picked) }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: SystemTheme.recentColours.length > 0
                spacing: 8
                Label {
                    text: qsTr("Recent")
                    color: Theme.text
                    font.pixelSize: 12
                    Layout.preferredWidth: 92
                }
                Repeater {
                    model: SystemTheme.recentColours
                    delegate: Rectangle {
                        required property string modelData
                        implicitWidth: 28
                        implicitHeight: 24
                        radius: 4
                        color: modelData
                        border.color: Theme.border
                        border.width: 1
                        TapHandler { onTapped: recentMenu.openFor(modelData) }
                        ToolTip.visible: hover.hovered
                        ToolTip.text: modelData.toUpperCase()
                        HoverHandler { id: hover }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            Label {
                Layout.fillWidth: true
                visible: SystemTheme.recentColours.length > 0
                text: qsTr("Keeps the last %1 colours you chose.").arg(SystemTheme.recentColoursLimit)
                color: Theme.textDim
                font.pixelSize: 10
            }

            // Saving is what turns a set of colours into something reselectable from the
            // Palettes menu, so it lives with the colours rather than in the menu.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Label {
                    text: qsTr("Save as")
                    color: Theme.text
                    font.pixelSize: 12
                    Layout.preferredWidth: 92
                }
                TextField {
                    id: paletteName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name this palette")
                    maximumLength: SystemTheme.paletteNameMaximum
                    selectByMouse: true
                    color: Theme.text
                    placeholderTextColor: Theme.textDim
                    background: Rectangle {
                        color: Theme.surfaceHigh
                        border.color: paletteName.activeFocus ? Theme.accent : Theme.border
                        border.width: 1
                        radius: 3
                    }
                    onAccepted: if (text.trim() !== "") { SystemTheme.savePalette(text); text = "" }
                }
                Button {
                    text: qsTr("Save")
                    enabled: paletteName.text.trim() !== ""
                    onClicked: { SystemTheme.savePalette(paletteName.text); paletteName.text = "" }
                }
            }

            // Saved palettes, with a way to remove one. Without this the list could only ever
            // grow, which is exactly the complaint the recent-colours cap answers elsewhere.
            Flow {
                Layout.fillWidth: true
                visible: SystemTheme.savedPalettes.length > 0
                spacing: 6
                Repeater {
                    model: SystemTheme.savedPalettes
                    delegate: Rectangle {
                        required property var modelData
                        height: 26
                        width: chipRow.implicitWidth + 16
                        radius: 13
                        color: Theme.surfaceHigh
                        border.color: Theme.border
                        border.width: 1
                        RowLayout {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 6
                            Rectangle {
                                width: 12; height: 12; radius: 6
                                color: modelData.accent
                                border.color: Theme.border
                                border.width: 1
                            }
                            Label {
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: 11
                            }
                            Label {
                                text: "\u00d7"
                                color: Theme.textDim
                                font.pixelSize: 14
                                TapHandler { onTapped: SystemTheme.deletePalette(modelData.name) }
                            }
                        }
                        TapHandler { onTapped: SystemTheme.applySavedPalette(modelData.name) }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Button {
                    text: qsTr("Reset to defaults")
                    onClicked: SystemTheme.seedCustomFromCurrent()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: SystemTheme.preference === SystemTheme.Custom
                          ? qsTr("Using my colours") : qsTr("Use my colours")
                    highlighted: SystemTheme.preference !== SystemTheme.Custom
                    onClicked: SystemTheme.preference = SystemTheme.Custom
                }
            }
        }

    }

    // A recent colour has no role of its own, so ask which one it should become.
    Menu {
        id: recentMenu
        property color pending: "black"
        function openFor(colour) { pending = colour; popup() }
        MenuItem { text: qsTr("Use as background"); onTriggered: root.applyCustomColour("background", recentMenu.pending) }
        MenuItem { text: qsTr("Use as panels");     onTriggered: root.applyCustomColour("surface", recentMenu.pending) }
        MenuItem { text: qsTr("Use as highlight");  onTriggered: root.applyCustomColour("accent", recentMenu.pending) }
        MenuItem { text: qsTr("Use as text");       onTriggered: root.applyCustomColour("text", recentMenu.pending) }
    }

    // Ten vertical faders, one per band. This is the part §1 warns about - the target user is
    // explicitly not a sound engineer - so it sits one step in, behind "Custom…" at the bottom of
    // the preset list, exactly as the colour editor sits behind the ready-made palettes.
    Dialog {
        id: equaliserDialog
        title: qsTr("Custom equaliser")
        // Deliberately not modal: an equaliser is adjusted *while listening*, and a modal dialog
        // blocks the transport, the playlist and the menus while you do it.
        modal: false
        anchors.centerIn: parent
        width: Math.min(460, root.width - 60)
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape

        ColumnLayout {
            id: equaliserColumn
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Drag a band to change it. Boosts are quietly compensated for, so a "
                           + "loud curve will not distort - it will just be a little quieter.")
                color: Theme.textDim
                font.pixelSize: 11
            }

            RowLayout {
                id: faderRow
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                spacing: 2

                Repeater {
                    model: AudioEngine.equaliserBandLabels()
                    delegate: ColumnLayout {
                        // Width computed rather than filled: a vertical Slider has a small fixed
                        // implicit width and Layout.fillWidth would not spread the columns past
                        // it, so the ten faders bunched against the left edge.
                        //
                        // Measured from the dialog's column, not from the row. Dividing the row's
                        // own width is circular - the row is as wide as its children, which were
                        // being sized from it - and the faders stayed exactly as narrow as before.
                        Layout.preferredWidth: (equaliserColumn.width - faderRow.spacing * 9) / 10
                        Layout.fillHeight: true
                        spacing: 4

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: (AudioEngine.equaliserBands[index] > 0 ? "+" : "")
                                  + Number(AudioEngine.equaliserBands[index]).toFixed(0)
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.family: "monospace"
                        }
                        Slider {
                            id: bandSlider
                            // Fills the column rather than centring at its implicit width. The
                            // custom background sets `width: 4` and no implicitWidth, so the
                            // Slider's own bounding box collapsed to nothing: the handle was
                            // painted outside its bounds and looked perfectly normal while
                            // catching no mouse events at all, which read as "the sliders do not
                            // do anything". The hit area is the whole column now.
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            orientation: Qt.Vertical
                            from: -24; to: 12
                            value: AudioEngine.equaliserBands[index]
                            onMoved: AudioEngine.setEqualiserBand(index, value)

                            background: Rectangle {
                                x: bandSlider.leftPadding + bandSlider.availableWidth / 2 - width / 2
                                y: bandSlider.topPadding
                                width: 4
                                height: bandSlider.availableHeight
                                radius: 2
                                color: Theme.surfaceHigh
                            }
                            handle: Rectangle {
                                x: bandSlider.leftPadding + bandSlider.availableWidth / 2 - width / 2
                                y: bandSlider.topPadding
                                   + bandSlider.visualPosition * (bandSlider.availableHeight - height)
                                width: 16; height: 16; radius: 8
                                color: Theme.accent
                            }
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData
                            color: Theme.textDim
                            font.pixelSize: 9
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Flatten")
                    onClicked: AudioEngine.flattenCustomEqualiser()
                }
            }
        }
    }

    Dialog {
        id: qualityDialog
        title: qsTr("Visual quality")
        modal: true
        anchors.centerIn: parent
        width: Math.min(440, root.width - 60)
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            // The visualiser renders to an offscreen buffer at a fraction of window size and
            // the scene graph upscales it. On a software renderer this is the only control
            // that meaningfully changes the frame rate, so it is a real setting, not a hack.
            CheckBox {
                id: autoQuality
                text: qsTr("Adjust automatically to keep it smooth")
                checked: visualizer.adaptiveQuality
                onToggled: visualizer.adaptiveQuality = checked
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: autoQuality.checked
                      ? qsTr("Quality rises and falls with what the machine can manage.")
                      : qsTr("Quality stays where you put it, however slow that runs.")
                color: Theme.textDim
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Label {
                    text: qsTr("Detail")
                    color: Theme.text
                    font.pixelSize: 12
                    Layout.preferredWidth: 54
                }
                Slider {
                    id: scaleSlider
                    Layout.fillWidth: true
                    from: 0.10; to: 1.0; stepSize: 0.05
                    enabled: !autoQuality.checked
                    value: visualizer.renderScale
                    onMoved: visualizer.renderScale = value
                }
                Label {
                    text: Math.round(visualizer.renderScale * 100) + "%"
                    color: Theme.text
                    font.family: "monospace"
                    font.pixelSize: 12
                    Layout.preferredWidth: 46
                    horizontalAlignment: Text.AlignRight
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Rendering at %1×%2, showing %3 fps")
                        .arg(visualizer.renderSize.width)
                        .arg(visualizer.renderSize.height)
                        .arg(visualizer.fps.toFixed(0))
                color: Theme.textDim
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                visible: autoQuality.checked
                Label {
                    text: qsTr("Aim for")
                    color: Theme.text
                    font.pixelSize: 12
                    Layout.preferredWidth: 54
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("20 fps — lightest"), qsTr("30 fps — balanced"),
                            qsTr("45 fps — smoothest")]
                    currentIndex: visualizer.targetFps >= 40 ? 2
                                : visualizer.targetFps >= 26 ? 1 : 0
                    onActivated: visualizer.targetFps = [20, 30, 45][currentIndex]
                }
            }
        }
    }

    // Streams come free from playbin3, so this is a dialog and a list rather than an engine.
    Dialog {
        id: streamDialog
        title: qsTr("Add a radio stream")
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - 60)
        standardButtons: Dialog.Ok | Dialog.Cancel
        closePolicy: Popup.CloseOnEscape

        onOpened: { urlField.text = ""; nameField.text = ""; urlField.forceActiveFocus() }
        onAccepted: {
            PlaylistModel.addStream(urlField.text, nameField.text)
            if (PlaylistModel.count > 0 && AudioEngine.source === "")
                root.playIndex(PlaylistModel.count - 1)
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                text: qsTr("Stream address")
                color: Theme.textDim
                font.pixelSize: 11
            }
            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("http://example.com:8000/stream")
                selectByMouse: true
                color: Theme.text
                placeholderTextColor: Theme.textDim
                background: Rectangle {
                    color: Theme.surfaceHigh
                    border.color: urlField.activeFocus ? Theme.accent : Theme.border
                    border.width: 1
                    radius: 3
                }
                onAccepted: if (streamDialog.acceptable) streamDialog.accept()
            }
            Label {
                text: qsTr("Station name (optional)")
                color: Theme.textDim
                font.pixelSize: 11
            }
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Leave blank to use the address")
                selectByMouse: true
                color: Theme.text
                placeholderTextColor: Theme.textDim
                background: Rectangle {
                    color: Theme.surfaceHigh
                    border.color: nameField.activeFocus ? Theme.accent : Theme.border
                    border.width: 1
                    radius: 3
                }
                onAccepted: if (streamDialog.acceptable) streamDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: urlField.text !== "" && !streamDialog.acceptable
                text: qsTr("That needs to be a full address, including http:// or https://")
                color: "#d08770"
                font.pixelSize: 11
            }
        }

        // Cheap sanity check only. Whether the station actually answers is playbin3's problem,
        // and it reports back through the usual error path.
        readonly property bool acceptable: /^[a-zA-Z][a-zA-Z0-9+.-]*:\/\/[^\s\/]+/.test(urlField.text.trim())
        Component.onCompleted: standardButton(Dialog.Ok).enabled = Qt.binding(function() {
            return streamDialog.acceptable
        })
    }

    Dialog {
        id: aboutDialog
        title: qsTr("About Reverie")
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - 60)
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ReverieMark {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: qsTr("Reverie Media Player")
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                    Label {
                        // "Media player", not "music player": video is a committed later
                        // phase, not a feature that will quietly never arrive.
                        text: qsTr("A media player with the visualisations built in.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                columns: 2
                columnSpacing: 14
                rowSpacing: 4

                Label { text: qsTr("Version");    color: Theme.textDim; font.pixelSize: 11 }
                Label { text: AppInfo.version;    color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
                Label { text: qsTr("Built");      color: Theme.textDim; font.pixelSize: 11 }
                Label { text: AppInfo.buildDate;  color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
                Label { text: qsTr("Commit");     color: Theme.textDim; font.pixelSize: 11 }
                Label { text: AppInfo.commit;     color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
            }

            Label {
                Layout.fillWidth: true
                text: '<a href="' + AppInfo.homepage + '">' + AppInfo.homepage + '</a>'
                color: Theme.textDim
                linkColor: Theme.accent
                font.pixelSize: 11
                textFormat: Text.StyledText
                elide: Text.ElideRight
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Reverie is released under the %1 licence.").arg(AppInfo.license)
                color: Theme.textDim
                font.pixelSize: 11
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

            // Attribution is the actual obligation of the stack in §3, so this is a real
            // credits list rather than a version dump. Everything here was read off the
            // licence files that ship with each dependency, not assumed from its name.
            ScrollView {
                id: creditsView
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    // availableWidth, not parent.width: inside a ScrollView the content item's
                    // parent is the flickable, whose width is the content width rather than the
                    // viewport, so wrapped text was being cut off instead of wrapping.
                    width: creditsView.availableWidth
                    spacing: 8

                    Label {
                        text: qsTr("Built with")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 12
                        rowSpacing: 3

                        Label { text: "Qt";        color: Theme.textDim; font.pixelSize: 11 }
                        Label { text: AppInfo.qtVersion; color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
                        Label { text: "LGPL-3.0";  color: Theme.textDim; font.pixelSize: 11 }

                        Label { text: "GStreamer"; color: Theme.textDim; font.pixelSize: 11 }
                        Label { text: AppInfo.gstreamerVersion; color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
                        Label { text: "LGPL-2.1";  color: Theme.textDim; font.pixelSize: 11 }

                        Label { text: "projectM";  color: Theme.textDim; font.pixelSize: 11 }
                        Label { text: AppInfo.projectMVersion; color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
                        Label { text: "LGPL-2.1";  color: Theme.textDim; font.pixelSize: 11 }

                        Label { text: "TagLib";    color: Theme.textDim; font.pixelSize: 11 }
                        Label { text: AppInfo.taglibVersion; color: Theme.text; font.pixelSize: 11; font.family: "monospace" }
                        Label { text: "LGPL-2.1 / MPL-1.1"; color: Theme.textDim; font.pixelSize: 11 }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("All four are linked dynamically. projectM additionally bundles projectm-eval and hlslparser, both MIT, and further third-party components listed in its own repository.")
                        color: Theme.textDim
                        font.pixelSize: 10
                    }

                    Label {
                        Layout.topMargin: 4
                        text: qsTr("Visualisations")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("The bundled presets are the Milkdrop \u201cCream of the Crop\u201d collection, curated and sorted by ISOSCELES and distributed with projectM. Per that collection\u2019s own notice, the presets were in almost all cases never released under a specific licence and are treated as public domain, with each preset author retaining copyright in their own work.")
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                }
            }
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

    Shortcut { sequence: "Space";       onActivated: { root.revealControls(); root.togglePlay() } }
    Shortcut { sequence: "Right";       onActivated: { root.revealControls(); AudioEngine.seek(AudioEngine.position + 5000) } }
    Shortcut { sequence: "Left";        onActivated: { root.revealControls(); AudioEngine.seek(Math.max(0, AudioEngine.position - 5000)) } }
    Shortcut { sequence: "Ctrl+Right";  onActivated: root.playNext() }
    Shortcut { sequence: "Ctrl+Left";   onActivated: root.playPrevious() }
    Shortcut { sequence: "Ctrl+M";      onActivated: root.mini = !root.mini }
    Shortcut { sequence: "Ctrl+L";      onActivated: root.playlistVisible = !root.playlistVisible }
    Shortcut { sequence: "Up";          onActivated: AudioEngine.volume = Math.min(1, AudioEngine.volume + 0.05) }
    Shortcut { sequence: "Down";        onActivated: AudioEngine.volume = Math.max(0, AudioEngine.volume - 0.05) }
    Shortcut { sequence: "F";           onActivated: root.fullscreen = !root.fullscreen }
    Shortcut { sequence: "N";           onActivated: if (!AudioEngine.hasVideo) visualizer.nextPreset() }
    Shortcut { sequence: "P";           onActivated: if (!AudioEngine.hasVideo) visualizer.previousPreset() }
    Shortcut { sequence: "R";           onActivated: if (!AudioEngine.hasVideo) visualizer.randomPreset() }
    Shortcut {
        sequences: ["Escape"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (root.fullscreen)
                root.fullscreen = false
            else if (root.mini)
                root.mini = false
        }
    }
}
