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
    // Just the track. Qt's formatWindowTitle already appends applicationDisplayName after an
    // em dash on X11, so adding "— Reverie" here produced "track — Reverie — Reverie Media
    // Player" and said the name twice. An empty title leaves Qt to show the name alone.
    title: PlaylistModel.currentTitle
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

    // Reports every window and transport geometry change when asked for. The mini player's
    // resize behaviour differs by compositor and could not be reproduced on three window
    // managers here, so the machine that shows it has to be the one that describes it.
    onWidthChanged: AppInfo.logLayout("window", width, height, transportBar.uiScale)
    onHeightChanged: AppInfo.logLayout("window", width, height, transportBar.uiScale)

    property bool mini: false
    property bool fullscreen: false
    property bool playlistVisible: true
    property bool presetPanelVisible: false
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
    // Same rules as the playlist: no panel in mini or fullscreen, and not below the width where
    // the stage would be squeezed out. It also cannot open over video, which has no presets.
    readonly property bool showPresetPanel: !mini && !fullscreen && presetPanelVisible
                                            && roomForPlaylist && !AudioEngine.hasVideo
                                            && SystemTheme.visualisationsEnabled

    // What the window was before it went fullscreen, so leaving restores that rather than
    // always dropping to Windowed - which silently un-maximised a maximised window.
    property int visibilityBeforeFullscreen: Window.Windowed

    onFullscreenChanged: {
        if (fullscreen) {
            if (root.visibility !== Window.FullScreen)
                root.visibilityBeforeFullscreen = root.visibility
            root.visibility = Window.FullScreen
            controlsVisible = true
            fullscreenHint.show()
            idleTimer.restart()
        } else {
            root.visibility = root.visibilityBeforeFullscreen === Window.FullScreen
                              ? Window.Windowed
                              : root.visibilityBeforeFullscreen
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
    // Clamped against the live window width as well as the stored bounds, so a width saved on a
    // wide screen cannot leave the stage unusable on a narrow one.
    function clampPresetPanelWidth(w) {
        const room = root.width - (root.showPlaylist ? playlistPanel.width : 0) - 360
        return Math.max(200, Math.min(w, Math.max(200, room)))
    }

    // Playing one specific visualisation, from the menu or a list. Deliberately locks: choosing
    // a preset by name is a request for that preset, and the rotation would move off it inside
    // half a minute otherwise.
    function playPreset(path) {
        // Belt as well as braces. The menus are closed when the stage changes, but a command
        // can also arrive from a menu that was already open, or from a shortcut, and there is
        // no visualiser running behind a video to receive it.
        if (!path || AudioEngine.hasVideo)
            return
        const all = PresetLibrary.paths("")
        const i = all.indexOf(path)
        if (i >= 0) {
            visualizer.setPresetList(all)
            visualizer.jumpTo(i)
        } else {
            // A favourite from the full pack while the curated set is active. Append rather
            // than replace, so the rotation still has somewhere to go if it is unlocked again.
            const widened = all.concat([path])
            visualizer.setPresetList(widened)
            visualizer.jumpTo(widened.length - 1)
        }
        visualizer.presetLocked = true
    }

    function clampPlaylistWidth(w) {
        const limit = Math.max(SystemTheme.kMinPlaylistWidth !== undefined ? 200 : 200,
                               root.width - 360 - 6)
        return Math.round(Math.max(200, Math.min(Math.max(200, limit), w)))
    }

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

    // false = the user pressed Next. Repeat-one replays only when a track *ends*.
    function playNext() { playIndex(PlaylistModel.nextForPlayback(false)) }
    function playPrevious() {
        // Restart the current track if the user is more than a few seconds in. Standard
        // behaviour everywhere, and it stops a mis-press losing your place in a long track.
        if (AudioEngine.position > 3000 && AudioEngine.seekable)
            AudioEngine.seek(0)
        else
            playIndex(PlaylistModel.previousIndex(true))
    }

    Component.onCompleted: {
        // The packaged library is the fallback; PresetPack decides whether the downloaded one
        // is in use and publishes the answer as activePresetsPath.
        PresetPack.bundledPresetsPath = initialPresetsPath
        applyPresetLibrary()
        if (!SystemTheme.flashNoticeSeen)
            flashNoticeTimer.start()
    }

    // A beat after the window is up, so the dialog has something to centre on and the person
    // sees the application behind it rather than a notice floating on nothing.
    Timer {
        id: flashNoticeTimer
        interval: 400
        repeat: false
        onTriggered: flashNotice.open()
    }

    // Swapping the library has to reach four things at once, and applying it at startup is as
    // important as applying it on a change: `presetsPath: initialPresetsPath` on the visualiser
    // is a one-time assignment, so a handler that only reacts to later changes leaves a restored
    // preference silently unapplied. The brief records the same shape costing a whole setting
    // once already, where a binding overwrote what the constructor had loaded.
    function applyPresetLibrary() {
        const path = PresetPack.activePresetsPath
        // The curated list names presets by relative path, and those same paths exist inside the
        // full pack - so leaving it set would narrow the download straight back down to the 480
        // it was meant to replace.
        const list = PresetPack.useFullPack ? "" : initialCuratedList
        PresetLibrary.rootPath = path
        PresetLibrary.curatedList = list
        visualizer.presetsPath = path
        visualizer.curatedList = list
        // A category narrowing names presets that no longer exist once the tree changes.
        visualizer.setPresetList([])
    }

    Connections {
        target: PresetPack
        function onActivePresetsPathChanged() { root.applyPresetLibrary() }
        function onUseFullPackChanged() { root.applyPresetLibrary() }
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

    // Tags arriving from the decoder are offered to the playing row. PlaylistModel only fills
    // gaps with them - a file that carries real tags keeps them.
    Connections {
        target: AudioEngine
        function onTagsChanged() {
            if (!PlaylistModel.currentIsStream && PlaylistModel.currentIndex >= 0) {
                PlaylistModel.supplyMetadata(PlaylistModel.currentIndex,
                                             AudioEngine.tagTitle, AudioEngine.tagArtist)
            }
        }
    }

    Connections {
        target: AudioEngine
        function onEndOfStream() {
            const next = PlaylistModel.nextForPlayback(true)
            if (next < 0)
                AudioEngine.stop()          // end of the list with repeat off
            else
                root.playIndex(next)
        }
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

            // The visualisation browser, mirroring the playlist on the other side. A panel
            // rather than a floating dialog: it shrinks the stage instead of covering the one
            // thing it exists to show you.
            PresetPanel {
                id: presetPanel
                visible: Layout.preferredWidth > 0
                clip: true
                visualizer: visualizer
                Layout.preferredWidth: root.showPresetPanel
                                       ? root.clampPresetPanelWidth(SystemTheme.presetPanelWidth) : 0
                Layout.fillHeight: true
                Behavior on Layout.preferredWidth {
                    enabled: !presetSplitterArea.pressed
                    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                }
                onCollapseRequested: root.presetPanelVisible = false
            }

            Item {
                id: presetSplitter
                visible: root.showPresetPanel
                Layout.preferredWidth: visible ? 6 : 0
                Layout.fillHeight: true

                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: 34
                    radius: 1
                    color: presetSplitterArea.containsMouse || presetSplitterArea.pressed
                           ? Theme.accent : Theme.border
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                    id: presetSplitterArea
                    anchors.fill: parent
                    anchors.leftMargin: -3
                    anchors.rightMargin: -3
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    property real grabX: 0
                    property int grabWidth: 0
                    onPressed: function(mouse) {
                        grabX = mapToItem(null, mouse.x, 0).x
                        grabWidth = SystemTheme.presetPanelWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        // This panel is on the left, so dragging right widens it - the opposite
                        // of the playlist's splitter.
                        const dx = mapToItem(null, mouse.x, 0).x - grabX
                        SystemTheme.presetPanelWidth = root.clampPresetPanelWidth(grabWidth + dx)
                    }
                }
            }

            Rectangle {
                visible: presetPanel.Layout.preferredWidth > 0
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

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
                    // Switched off, the renderer parks at about a tenth of a percent of one
                    // core and the stage shows its own colour - the same state as nothing
                    // playing. Turning visualisations off therefore costs nothing rather than
                    // hiding something that is still being drawn.
                    active: AudioEngine.state === AudioEngine.Playing && !AudioEngine.hasVideo
                            && SystemTheme.visualisationsEnabled
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
                    texturesPath: initialTexturesPath
                    curatedList: initialCuratedList
                }

                // The themed stage colour, painted *over* the visualiser rather than behind it,
                // and only while nothing is playing.
                //
                // Behind it did not work twice over. projectM's texture does not cover the item
                // exactly, so a coloured backdrop framed the artwork in a colour it never had -
                // invisible against the old black, obvious against anything else. And an idle
                // ProjectMItem still composites its cleared black buffer on top, so the colour
                // never showed when it was supposed to: the stage stayed black.
                //
                // Above and idle-only gets both right, without hiding the FBO item - which would
                // tear down its scene node and make projectM re-initialise on every pause.
                Rectangle {
                    anchors.fill: parent
                    color: Theme.stage
                    // Solid when nothing is playing - that is the case the colour was asked for,
                    // a window with no visible edge on a black desktop. While the visualiser runs
                    // it becomes a tint at whatever strength the user chose, defaulting to none.
                    // Video is never tinted: letterbox bars and picture both want to be left
                    // alone.
                    opacity: visualizer.active ? Theme.stageTint : 1.0
                    visible: !AudioEngine.hasVideo && opacity > 0.004
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
                        root.popupStageMenu()
                    }
                }
                // Touch has no second button, so the picker gets the gesture touch actually uses
                // for a context menu.
                TapHandler {
                    acceptedDevices: PointerDevice.TouchScreen
                    onLongPressed: root.popupStageMenu()
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

                // With visualisations switched off the stage would otherwise be a flat colour,
                // which reads as something failing rather than as a setting being honoured. The
                // mark says the application is fine and this is deliberate.
                //
                // Deliberately still: no fade, no pulse, no animation of any kind. Someone who
                // turned visualisations off did so to stop things moving, and giving them a
                // breathing logo instead would miss the point entirely.
                //
                // It defers to the empty-playlist block below, which already shows the mark
                // along with its prompt; two marks on one stage would be one too many.
                ReverieMark {
                    id: idleMark
                    width: 96
                    height: 96
                    visible: !AudioEngine.hasVideo
                             && !SystemTheme.visualisationsEnabled
                             && PlaylistModel.count > 0
                    // Quiet rather than absent. The mark is never recoloured (it is identity,
                    // not decoration), so this dims it without touching the artwork.
                    opacity: 0.38

                    // Drifting only in fullscreen, and only when the mark is all there is. Going
                    // fullscreen with no visualisations is a deliberate "leave this on screen"
                    // act, which is the one place movement is wanted; someone using the player
                    // normally turned visualisations off to stop things moving and should not be
                    // given a wandering logo for their trouble.
                    readonly property bool drifting:
                        visible && root.fullscreen && SystemTheme.markBounce

                    // Slow on purpose - about forty pixels a second, so it reads as drift rather
                    // than as something being animated at you. No flashing, no fading, no easing:
                    // constant velocity and a clean reflection off each edge, like the DVD logo
                    // this is stealing from.
                    property real vx: 42
                    property real vy: 31

                    x: (parent.width - width) / 2
                    y: (parent.height - height) / 2

                    onDriftingChanged: if (!drifting) {
                        x = (parent.width - width) / 2
                        y = (parent.height - height) / 2
                    }

                    Timer {
                        // 30fps is plenty for something moving this slowly, and on a software
                        // renderer every frame of a full-stage repaint is real work.
                        interval: 33
                        repeat: true
                        running: idleMark.drifting
                        onTriggered: {
                            const dt = interval / 1000
                            let nx = idleMark.x + idleMark.vx * dt
                            let ny = idleMark.y + idleMark.vy * dt
                            const maxX = idleMark.parent.width - idleMark.width
                            const maxY = idleMark.parent.height - idleMark.height
                            if (nx <= 0) { nx = 0; idleMark.vx = Math.abs(idleMark.vx) }
                            else if (nx >= maxX) { nx = maxX; idleMark.vx = -Math.abs(idleMark.vx) }
                            if (ny <= 0) { ny = 0; idleMark.vy = Math.abs(idleMark.vy) }
                            else if (ny >= maxY) { ny = maxY; idleMark.vy = -Math.abs(idleMark.vy) }
                            idleMark.x = nx
                            idleMark.y = ny
                        }
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
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        // Said out loud because nothing else on the opening screen says it, and
                        // the name, the mark and the visualiser all point at audio. Video has
                        // been supported for a while and people have no reason to guess.
                        text: qsTr("I play videos, too!")
                        color: Theme.overlayTextDim
                        font.pixelSize: 12
                    }
                }

            }

            Rectangle {
                visible: playlistPanel.Layout.preferredWidth > 0
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.border
            }

            // Drag to rebalance the stage against the playlist. Neither may be squashed away:
            // SystemTheme clamps the stored width, and the maximum here keeps the stage at least
            // kMinStageWidth wide however narrow the window gets.
            Item {
                id: playlistSplitter
                visible: root.showPlaylist
                Layout.preferredWidth: visible ? 6 : 0
                Layout.fillHeight: true

                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: 34
                    radius: 1
                    color: splitterArea.containsMouse || splitterArea.pressed
                           ? Theme.accent : Theme.border
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                    id: splitterArea
                    anchors.fill: parent
                    anchors.leftMargin: -3
                    anchors.rightMargin: -3
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    property real grabX: 0
                    property int grabWidth: 0
                    onPressed: function(mouse) {
                        grabX = mapToItem(null, mouse.x, 0).x
                        grabWidth = SystemTheme.playlistWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        // The playlist is on the right, so dragging right narrows it.
                        const dx = mapToItem(null, mouse.x, 0).x - grabX
                        SystemTheme.playlistWidth = root.clampPlaylistWidth(grabWidth - dx)
                    }
                }
            }

            PlaylistPanel {
                id: playlistPanel
                visible: Layout.preferredWidth > 0
                clip: true
                Layout.preferredWidth: root.showPlaylist
                                       ? root.clampPlaylistWidth(SystemTheme.playlistWidth) : 0
                Layout.fillHeight: true
                // Animate the collapse, but not a drag - easing a width the pointer is already
                // holding makes the panel lag behind the splitter.
                Behavior on Layout.preferredWidth {
                    enabled: !splitterArea.pressed
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
            Layout.preferredHeight: root.fullscreen ? 0 : transportBar.height
        }
    }

    TransportBar {
        id: transportBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        compact: root.mini
        showRestore: root.mini
        // The mini player keeps its own compact height; only the full window honours the size the
        // user dragged, because mini is pinned to the bar and would fight the drag.
        height: root.mini ? implicitHeight : SystemTheme.transportHeight
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

    // Drag the top edge of the transport to make it taller or shorter. Everything inside scales
    // with it - see TransportBar.uiScale - and SystemTheme clamps the result, because a control
    // scaled down to a few pixels is a target nobody can hit.
    MouseArea {
        id: transportGrip
        visible: !root.mini && !root.fullscreen
        anchors { left: parent.left; right: parent.right; bottom: transportBar.top }
        height: 6
        hoverEnabled: true
        cursorShape: Qt.SplitVCursor
        property real grabY: 0
        property int grabHeight: 0
        onPressed: function(mouse) {
            grabY = mapToItem(null, 0, mouse.y).y
            grabHeight = SystemTheme.transportHeight
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            // The bar is at the bottom, so dragging up makes it taller.
            const dy = mapToItem(null, 0, mouse.y).y - grabY
            SystemTheme.transportHeight = grabHeight - dy
        }

        Rectangle {
            anchors.centerIn: parent
            width: 34
            height: 2
            radius: 1
            color: transportGrip.containsMouse || transportGrip.pressed
                   ? Theme.accent : Theme.border
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    // --- chrome ------------------------------------------------------------------------

    // Marking the visualisation you are actually watching. It sits opposite the chrome and
    // follows exactly the same visibility rules, so it fades with the controls in fullscreen
    // rather than being left floating over the picture on its own.
    Rectangle {
        id: favouriteChrome
        anchors { left: parent.left; top: parent.top; margins: 10 }
        // Clear of the browser panel when that is open, the way the chrome clears the playlist.
        anchors.leftMargin: root.showPresetPanel ? presetPanel.width + presetSplitter.width + 10
                                                 : 10
        // Nothing to favourite over video, and nothing to favourite above a black stage: a
        // preset is loaded whether or not audio is running, but offering to mark one you
        // cannot see is the same misreading that removed the idle animation and that makes the
        // menu header show an em dash when idle.
        visible: !root.mini && !AudioEngine.hasVideo && visualizer.active
                 && visualizer.presetFile !== ""
                 && (!root.fullscreen || root.controlsVisible)
        opacity: root.controlsVisible ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        width: favouriteButton.implicitWidth + 12
        height: favouriteButton.implicitHeight + 8
        radius: Theme.radius
        color: Theme.overlayBackground

        // Named first so QML binds to it: isFavourite() is a plain call and registers no
        // dependency by itself.
        readonly property bool marked:
            (PresetHistory.favourites, PresetHistory.isFavourite(visualizer.presetFile))

        IconButton {
            id: favouriteButton
            anchors.centerIn: parent
            glyph: favouriteChrome.marked ? "starFilled" : "star"
            overVideo: true
            onClicked: PresetHistory.toggleFavourite(visualizer.presetFile)
            ToolTip.visible: hovered
            ToolTip.text: favouriteChrome.marked ? qsTr("Remove from favourites")
                                                 : qsTr("Add to favourites")
        }
    }

    // Every visualisation that becomes current is remembered, automatic rotations included -
    // that is the point. Without it there is no way back to the one that just went past, which
    // is exactly when someone decides they liked it.
    Connections {
        target: visualizer
        function onPresetChanged() {
            if (visualizer.presetFile !== "")
                PresetHistory.noteUsed(visualizer.presetFile)
        }
    }

    Rectangle {
        // Keep clear of the playlist panel: this chrome belongs to the visualiser area. It
        // carries its own backdrop because the preset behind it can be any colour.
        anchors { right: parent.right; top: parent.top; margins: 10 }
        // Follows the panel's real width, not a constant. It was hardcoded to 350, which put the
        // chrome on top of the playlist the moment the panel could be dragged wider.
        // The edge handle is vertically centred, so it never reaches this corner.
        anchors.rightMargin: root.showPlaylist ? playlistPanel.width + playlistSplitter.width + 10
                                               : 10
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
    // The stage carries one menu or the other. Video has no preset to change, and until now a
    // right-click over it did nothing at all - which is where the subtitle controls go, since
    // that is where anyone who has used another player looks for them.
    // The stage carries one menu for the visualiser and another for video, and which one is
    // correct is decided by whatever is playing. A track ending mid-menu therefore leaves the
    // wrong menu open over the new stage: the preset picker survives into a video, where its
    // entries address a visualiser that is no longer running, and the video menu survives into
    // an audio track, where there are no subtitle or video streams to choose. Closing both on
    // the transition is the only guard that covers every route between them - a track ending,
    // a manual double-click in the playlist, Next, or a second file opened from the desktop.
    Connections {
        target: AudioEngine
        function onHasVideoChanged() {
            presetMenu.close()
            videoMenu.close()
        }
    }

    function popupStageMenu() {
        if (AudioEngine.hasVideo)
            videoMenu.popup()
        else if (!SystemTheme.visualisationsEnabled)
            logoMenu.popup()
        else
            presetMenu.popup()
    }

    // With visualisations off there is no preset to randomise, lock or step through, so the
    // preset picker would be a menu of things that cannot happen. This is what that stage can
    // actually offer: turn them back on, or decide whether the mark drifts.
    Menu {
        id: logoMenu
        onAboutToShow: root.fitMenuWidth(logoMenu, 220, 420)
        MenuItem {
            text: qsTr("Show visualisations")
            checkable: true
            checked: SystemTheme.visualisationsEnabled
            onTriggered: SystemTheme.visualisationsEnabled = checked
        }
        MenuSeparator {}
        MenuItem {
            // Named for where it applies. It does nothing windowed, and a checkbox that appears
            // to do nothing is worse than one that says when it will.
            text: qsTr("Drift the logo in fullscreen")
            checkable: true
            checked: SystemTheme.markBounce
            onTriggered: SystemTheme.markBounce = checked
        }
    }

    Menu {
        id: videoMenu
        onAboutToShow: root.fitMenuWidth(videoMenu, 200, 460)

        MenuItem {
            text: qsTr("SUBTITLES")
            enabled: false
        }

        // Exclusive, because these are one choice rather than several toggles. Four independent
        // checkable items can be left with nothing selected - clicking the active one assigns
        // checked = false, which detaches the binding that would restore it.
        ButtonGroup { id: subtitleGroup; exclusive: true }

        MenuItem {
            text: qsTr("Off")
            checkable: true
            ButtonGroup.group: subtitleGroup
            checked: AudioEngine.subtitleTrack === -1
            onTriggered: AudioEngine.setSubtitleTrack(-1)
        }

        // Repeater cannot build menu items - a Menu inserts through insertItem and has no
        // visual-children property for a Repeater to assign to.
        Instantiator {
            id: subtitleItems
            model: AudioEngine.subtitleTracks
            delegate: MenuItem {
                required property int index
                required property var modelData
                text: modelData.label
                checkable: true
                ButtonGroup.group: subtitleGroup
                checked: AudioEngine.subtitleTrack === index
                onTriggered: AudioEngine.setSubtitleTrack(index)
            }
            onObjectAdded: function(index, object) { videoMenu.insertItem(2 + index, object) }
            onObjectRemoved: function(index, object) { videoMenu.removeItem(object) }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Add subtitle file…")
            onTriggered: subtitleDialog.open()
        }

        // Only when there is actually a choice. One audio track is the overwhelming majority of
        // files, and a section offering a single option is noise - §1's "remove the knob".
        readonly property bool severalAudio: AudioEngine.audioTracks.length > 1

        MenuSeparator { visible: videoMenu.severalAudio }
        MenuItem {
            text: qsTr("AUDIO TRACK")
            enabled: false
            visible: videoMenu.severalAudio
        }
        ButtonGroup { id: audioTrackGroup }
        Instantiator {
            // Empty model when there is nothing to choose, so no items exist at all.
            model: videoMenu.severalAudio ? AudioEngine.audioTracks : []
            delegate: MenuItem {
                required property int index
                required property var modelData
                text: modelData.label
                checkable: true
                ButtonGroup.group: audioTrackGroup
                checked: AudioEngine.audioTrack === index
                onTriggered: AudioEngine.setAudioTrack(index)
            }
            // Appended, so the subtitle items keeping their fixed positions cannot displace them.
            onObjectAdded: function(index, object) { videoMenu.addItem(object) }
            onObjectRemoved: function(index, object) { videoMenu.removeItem(object) }
        }

        // Alternate video streams - different angles, mostly - are rare enough that this is
        // hidden for essentially every file. It exists because selecting one stream is required
        // anyway (naming them all stalls the pipeline), so once there is an index, reaching the
        // second stream costs a menu rather than a feature.
        readonly property bool severalVideo: AudioEngine.videoTracks.length > 1

        MenuSeparator { visible: videoMenu.severalVideo }
        MenuItem {
            text: qsTr("VIDEO STREAM")
            enabled: false
            visible: videoMenu.severalVideo
        }
        ButtonGroup { id: videoTrackGroup }
        Instantiator {
            model: videoMenu.severalVideo ? AudioEngine.videoTracks : []
            delegate: MenuItem {
                required property int index
                required property var modelData
                text: modelData.label
                checkable: true
                ButtonGroup.group: videoTrackGroup
                checked: AudioEngine.videoTrack === index
                onTriggered: AudioEngine.setVideoTrack(index)
            }
            onObjectAdded: function(index, object) { videoMenu.addItem(object) }
            onObjectRemoved: function(index, object) { videoMenu.removeItem(object) }
        }
    }

    // with, then by author where an author actually has several presets in that category.
    Menu {
        id: presetMenu
        onAboutToShow: root.fitMenuWidth(presetMenu, 200, 460)

        function useList(category) {
            visualizer.setPresetList(PresetLibrary.paths(category))
        }

        MenuItem {
            // A preset is loaded whether or not anything is playing, but naming it above a black
            // stage reads as "this is what you are watching", which is the same misreading that
            // removed the idle animation in the first place. Nothing playing, nothing named.
            readonly property bool showing: visualizer.active && visualizer.presetName !== ""
            // Labelled, because an unlabelled name at the head of a menu reads as a title for
            // the menu rather than as the thing currently on screen - the same misreading that
            // retired the unlabelled preset-name box under the visualiser.
            //
            // "None" rather than the em dash used elsewhere for "nothing loaded": a dash works
            // where it stands alone, as in the seek bar's -:--, but after a label it reads as a
            // rendering fault rather than as an answer.
            text: qsTr("Current: %1").arg(showing ? visualizer.presetName : qsTr("None"))
            enabled: false
            ToolTip.visible: hovered && showing
            ToolTip.text: visualizer.presetName
        }
        MenuSeparator {}

        MenuItem {
            readonly property bool marked:
                (PresetHistory.favourites, PresetHistory.isFavourite(visualizer.presetFile))
            text: marked ? qsTr("Remove from favourites") : qsTr("Add to favourites")
            // Same rule as the star and the header above it: a preset is loaded whether or not
            // anything is playing, but there is nothing on screen to mark.
            enabled: visualizer.active && visualizer.presetFile !== ""
            onTriggered: PresetHistory.toggleFavourite(visualizer.presetFile)
        }

        Menu {
            id: favouritesMenu
            title: qsTr("Favourites")
            onAboutToShow: root.fitMenuWidth(favouritesMenu, 200, 460)
            MenuItem {
                text: qsTr("None yet")
                enabled: false
                visible: PresetHistory.favouriteCount === 0
                height: visible ? implicitHeight : 0
            }
            // A Repeater cannot build menu items - a Menu inserts them rather than parenting
            // them, and assigning them as visual children fails on a property it does not have.
            Instantiator {
                model: PresetHistory.favourites
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.title
                    onTriggered: root.playPreset(modelData.path)
                }
                onObjectAdded: function(index, object) { favouritesMenu.insertItem(1 + index, object) }
                onObjectRemoved: function(index, object) { favouritesMenu.removeItem(object) }
            }
        }

        Menu {
            // Not `recentMenu` - that id already belongs to the recent-colours menu in the
            // theme editor.
            id: recentPresetsMenu
            title: qsTr("Recent")
            onAboutToShow: root.fitMenuWidth(recentPresetsMenu, 200, 460)
            MenuItem {
                text: qsTr("Nothing yet")
                enabled: false
                visible: PresetHistory.recents.length === 0
                height: visible ? implicitHeight : 0
            }
            Instantiator {
                model: PresetHistory.recents
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.title
                    // Picking one out of the history is choosing it, so it locks like any other
                    // deliberate selection - otherwise the rotation carries you off it within
                    // half a minute and the list looks broken.
                    onTriggered: root.playPreset(modelData.path)
                }
                onObjectAdded: function(index, object) { recentPresetsMenu.insertItem(1 + index, object) }
                onObjectRemoved: function(index, object) { recentPresetsMenu.removeItem(object) }
            }
        }

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
                // First, because it governs everything below it. Milkdrop visualisations flash
                // by design; anyone who needs to avoid that needs one obvious switch rather
                // than a colour slider that happens to cover the stage.
                text: qsTr("Show visualisations")
                checkable: true
                checked: SystemTheme.visualisationsEnabled
                onTriggered: SystemTheme.visualisationsEnabled = checked
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Use the curated selection")
                // A package that ships only the curated presets installs no list, and then this
                // switches between a set and itself. Hide it rather than have it do nothing.
                // Hidden over the full pack too, where the library choice above governs instead
                // and this would narrow 9,795 back to the 480 that were just replaced.
                visible: initialCuratedList !== "" && !PresetPack.useFullPack
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

            MenuSeparator {
                visible: PresetPack.installed
                height: visible ? implicitHeight : 0
            }

            // Which library is in play. Two directories rather than a list narrowing a corpus:
            // the packaged 480 and the downloaded 9,795 are separate trees, so this needs no
            // curated list and works in a package, where the list is deliberately not installed.
            ButtonGroup { id: presetLibraryGroup }

            MenuItem {
                text: qsTr("Curated selection")
                visible: PresetPack.installed
                height: visible ? implicitHeight : 0
                checkable: true
                ButtonGroup.group: presetLibraryGroup
                checked: !PresetPack.useFullPack
                onTriggered: PresetPack.useFullPack = false
            }
            MenuItem {
                text: qsTr("Every visualisation (%1)").arg(PresetPack.installedCount)
                visible: PresetPack.installed
                height: visible ? implicitHeight : 0
                checkable: true
                ButtonGroup.group: presetLibraryGroup
                checked: PresetPack.useFullPack
                onTriggered: PresetPack.useFullPack = true
            }
            MenuItem {
                text: qsTr("Get more visualisations…")
                visible: !PresetPack.installed
                height: visible ? implicitHeight : 0
                onTriggered: presetPackDialog.open()
            }
            MenuItem {
                text: qsTr("Remove the extra visualisations")
                visible: PresetPack.installed
                height: visible ? implicitHeight : 0
                onTriggered: PresetPack.remove()
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

            // The twelve curves plus Custom were sitting directly in Sound, which made the menu
            // mostly a list of equaliser presets with the track picker lost at the bottom. Sound
            // is now two things you can name, each behind its own title.
            Menu {
                id: equaliserMenu
                // "Equaliser" rather than "Equalizer" only to match the rest of the interface,
                // which is British throughout - visualisation, colours, no equaliser.
                title: qsTr("Equaliser")
                onAboutToShow: root.fitMenuWidth(equaliserMenu, 200, 420)

                // Exclusive for the same reason the appearance options are: independent
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
                // Repeater tries to assign them as visual children, which a Menu has no property
                // for.
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
                    onObjectAdded: function(index, object) { equaliserMenu.insertItem(2 + index, object) }
                    onObjectRemoved: function(index, object) { equaliserMenu.removeItem(object) }
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

            // Switching here is for this file only and deliberately does not write the language
            // preference: "play the French track this once" and "prefer French from now on" are
            // different intentions, and conflating them makes one of them impossible.
            Menu {
                id: audioTrackMenu
                title: qsTr("Audio track selection")
                onAboutToShow: root.fitMenuWidth(audioTrackMenu, 200, 420)
                ButtonGroup { id: audioTrackMenuGroup }
                MenuItem {
                    text: qsTr("No other track")
                    enabled: false
                    visible: AudioEngine.audioTracks.length < 2
                }
                Instantiator {
                    model: AudioEngine.audioTracks.length > 1 ? AudioEngine.audioTracks : []
                    delegate: MenuItem {
                        required property int index
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        ButtonGroup.group: audioTrackMenuGroup
                        checked: AudioEngine.audioTrack === index
                        onTriggered: AudioEngine.setAudioTrack(index)
                    }
                    onObjectAdded: function(index, object) { audioTrackMenu.insertItem(1 + index, object) }
                    onObjectRemoved: function(index, object) { audioTrackMenu.removeItem(object) }
                }
            }
        }

        // Sibling of Sound rather than inside it. These are standing preferences - which
        // language to pick when a file offers a choice - as distinct from the per-file track
        // switching, which lives on the picture where you are looking when you want it.
        Menu {
            id: videoMenuOptions
            title: qsTr("Video")
            onAboutToShow: root.fitMenuWidth(videoMenuOptions, 220, 420)

            Menu {
                id: audioLanguageMenu
                title: qsTr("Preferred audio language")
                onAboutToShow: root.fitMenuWidth(audioLanguageMenu, 200, 420)
                ButtonGroup { id: audioLanguageGroup }
                Instantiator {
                    model: AudioEngine.languageChoices(false)
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        ButtonGroup.group: audioLanguageGroup
                        checked: AudioEngine.preferredAudioLanguage === modelData.code
                        onTriggered: AudioEngine.preferredAudioLanguage = modelData.code
                    }
                    onObjectAdded: function(index, object) { audioLanguageMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { audioLanguageMenu.removeItem(object) }
                }
            }

            Menu {
                id: subtitleLanguageMenu
                title: qsTr("Preferred subtitle language")
                onAboutToShow: root.fitMenuWidth(subtitleLanguageMenu, 200, 420)
                ButtonGroup { id: subtitleLanguageGroup }
                Instantiator {
                    model: AudioEngine.languageChoices(true)
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        ButtonGroup.group: subtitleLanguageGroup
                        checked: AudioEngine.preferredSubtitleLanguage === modelData.code
                        onTriggered: AudioEngine.preferredSubtitleLanguage = modelData.code
                    }
                    onObjectAdded: function(index, object) { subtitleLanguageMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { subtitleLanguageMenu.removeItem(object) }
                }
            }

            Menu {
                id: subtitleTrackMenu
                title: qsTr("Subtitle track selection")
                onAboutToShow: root.fitMenuWidth(subtitleTrackMenu, 200, 420)
                ButtonGroup { id: subtitleTrackMenuGroup }
                MenuItem {
                    text: qsTr("Off")
                    checkable: true
                    ButtonGroup.group: subtitleTrackMenuGroup
                    checked: AudioEngine.subtitleTrack === -1
                    onTriggered: AudioEngine.setSubtitleTrack(-1)
                }
                MenuItem {
                    text: qsTr("Nothing loaded has subtitles")
                    enabled: false
                    visible: AudioEngine.subtitleTracks.length === 0
                }
                Instantiator {
                    model: AudioEngine.subtitleTracks
                    delegate: MenuItem {
                        required property int index
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        ButtonGroup.group: subtitleTrackMenuGroup
                        checked: AudioEngine.subtitleTrack === index
                        onTriggered: AudioEngine.setSubtitleTrack(index)
                    }
                    onObjectAdded: function(index, object) { subtitleTrackMenu.insertItem(2 + index, object) }
                    onObjectRemoved: function(index, object) { subtitleTrackMenu.removeItem(object) }
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Software rendering (requires restart)")
                checkable: true
                checked: SystemTheme.softwareRendering
                onTriggered: {
                    SystemTheme.softwareRendering = checked
                    softwareRenderingNote.open()
                }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Draw the visualisation on the processor instead of the "
                                   + "graphics card. Slower, but some drivers render nothing.")
            }

            MenuItem {
                text: qsTr("Show subtitles")
                checkable: true
                // The preference rather than the live track, so it still reads correctly with
                // nothing playing. Same entry point as the transport button and the picture's
                // own menu, so the three cannot disagree.
                checked: AudioEngine.subtitlesEnabled
                onTriggered: AudioEngine.setSubtitlesEnabled(checked)
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
                spacing: 10
                Label {
                    // "Tint" alone did not say what it tinted, sitting in a list of colour roles.
                    text: qsTr("Visualiser tint")
                    color: Theme.text
                    Layout.preferredWidth: 96
                    wrapMode: Text.WordWrap
                }
                Slider {
                    id: tintSlider
                    Layout.fillWidth: true
                    // Same trap as the transport sliders and the equaliser faders: a custom
                    // background gives the Control no implicit size, so it draws correctly and
                    // catches nothing.
                    implicitHeight: 20
                    from: 0; to: 100; stepSize: 5
                    value: SystemTheme.stageTint
                    onMoved: SystemTheme.stageTint = Math.round(value)
                    background: Rectangle {
                        x: tintSlider.leftPadding
                        y: tintSlider.topPadding + tintSlider.availableHeight / 2 - height / 2
                        width: tintSlider.availableWidth; height: 4; radius: 2
                        color: Theme.surfaceHigh
                        Rectangle {
                            width: tintSlider.visualPosition * parent.width
                            height: parent.height; radius: 2
                            color: Theme.accent
                        }
                    }
                    handle: Rectangle {
                        x: tintSlider.leftPadding
                           + tintSlider.visualPosition * (tintSlider.availableWidth - width)
                        y: tintSlider.topPadding + tintSlider.availableHeight / 2 - height / 2
                        width: 14; height: 14; radius: 7
                        color: Theme.accent
                    }
                }
                Label {
                    text: SystemTheme.stageTint + "%"
                    color: Theme.textDim
                    font.family: "monospace"
                    Layout.preferredWidth: 46
                    horizontalAlignment: Text.AlignRight
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("How much of the Visualiser colour washes over the visualisation while "
                           + "it plays. At 0% the visualisation is untouched, and that colour is "
                           + "only what you see when nothing is playing.")
                color: Theme.textDim
                font.pixelSize: 11
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
        id: presetPackDialog
        title: qsTr("More visualisations")
        // Not modal: this downloads while you listen, and a modal dialog would block the
        // transport and the playlist for the duration, the same reason the equaliser is not one.
        modal: false
        anchors.centerIn: parent
        width: Math.min(460, root.width - 60)
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.NoButton

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                text: qsTr("Reverie comes with a selection of %1 visualisations. The full Milkdrop collection has %2.")
                          .arg(PresetLibrary.count)
                          .arg(PresetPack.packCount)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 11
                // Said plainly because it is the honest trade: more to look at, and a slower
                // tail. Measured - the curated set is chosen to avoid the most expensive
                // presets, not to be prettier.
                text: qsTr("A %1 download. More variety, some of them slower to draw, and some "
                           + "that flash harder than the ones Reverie ships with. You can switch "
                           + "back at any time.").arg(PresetPack.downloadSize)
            }

            ProgressBar {
                Layout.fillWidth: true
                visible: PresetPack.state !== PresetPack.Idle && PresetPack.state !== PresetPack.Failed
                height: visible ? implicitHeight : 0
                from: 0; to: 1
                value: PresetPack.progress
                indeterminate: PresetPack.state === PresetPack.Extracting
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: PresetPack.message !== ""
                height: visible ? implicitHeight : 0
                text: PresetPack.message
                // No dedicated error colour: every token is derived from the user's four so that
                // no theme can produce invisible text, and a fifth hand-picked one would not be.
                // Full-strength text against dimmed is enough to mark a failure, and the message
                // says so in words regardless.
                color: PresetPack.state === PresetPack.Failed ? Theme.text : Theme.textDim
                font.pixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Close")
                    visible: PresetPack.state === PresetPack.Idle || PresetPack.state === PresetPack.Failed
                    onClicked: presetPackDialog.close()
                }
                Button {
                    text: qsTr("Cancel")
                    visible: !(PresetPack.state === PresetPack.Idle || PresetPack.state === PresetPack.Failed)
                    onClicked: PresetPack.cancel()
                }
                Button {
                    text: PresetPack.state === PresetPack.Failed ? qsTr("Try again") : qsTr("Download")
                    enabled: PresetPack.state === PresetPack.Idle || PresetPack.state === PresetPack.Failed
                    visible: !PresetPack.installed
                    highlighted: true
                    onClicked: PresetPack.install()
                }
            }
        }

        Connections {
            target: PresetPack
            function onFinished(ok) { if (ok) presetPackDialog.close() }
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

    // Shown once, on first run, before anything has played and therefore before any flashing.
    // That timing is the whole point: a notice that arrives during or after exposure does not
    // help the person it exists for.
    //
    // It carries the remedy rather than only the warning. A notice that says "this may harm
    // you" and leaves you to find the setting yourself is a disclaimer; one with the switch in
    // it is a choice, and it is the difference between intruding usefully and intruding.
    Dialog {
        id: flashNotice
        title: qsTr("Before you start")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, root.width - 60)
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.NoButton

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text
                text: qsTr("Reverie's visualisations flash and change quickly. If you are "
                           + "sensitive to flashing lights, you may want to turn them off — "
                           + "everything else works exactly the same.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 11
                text: qsTr("You can change this at any time in Options ▸ Visualisation.")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Turn them off")
                    onClicked: {
                        SystemTheme.visualisationsEnabled = false
                        SystemTheme.flashNoticeSeen = true
                        flashNotice.close()
                    }
                }
                Button {
                    text: qsTr("Keep them on")
                    highlighted: true
                    onClicked: {
                        SystemTheme.visualisationsEnabled = true
                        SystemTheme.flashNoticeSeen = true
                        flashNotice.close()
                    }
                }
            }
        }
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

            // The notice is shown once at first run; this is the copy that stays findable.
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The visualisations flash and change quickly. They can be turned off "
                           + "in Options ▸ Visualisation.")
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

    Dialog {
        id: softwareRenderingNote
        title: qsTr("Software rendering")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok
        // The dialog gets an explicit width and the text fills it. Sizing the dialog from the
        // text while the text wraps to the dialog is what produced a binding loop; Text's
        // implicitWidth is read-only, so the width has to come from the dialog.
        width: 380
        contentItem: Text {
            text: SystemTheme.softwareRendering
                  ? qsTr("Software rendering will be used the next time Reverie starts.")
                  : qsTr("The graphics card will be used again the next time Reverie starts.")
            color: Theme.text
            wrapMode: Text.WordWrap
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: 6 }
    }

    FileDialog {
        id: subtitleDialog
        title: qsTr("Add subtitle file")
        nameFilters: [qsTr("Subtitles (*.srt *.ass *.ssa *.vtt *.sub)"), qsTr("All files (*)")]
        onAccepted: AudioEngine.addSubtitleFile(selectedFile)
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

    // The same idea on the other side: with the browser collapsed its own chevron is gone, so
    // a handle sits on the edge it retracted into. Hidden in mini and fullscreen exactly as the
    // playlist's is, and over video, where the panel cannot open at all - a handle that opens
    // nothing is worse than no handle.
    Rectangle {
        id: presetHandle
        visible: !root.mini && !root.fullscreen && root.roomForPlaylist
                 && !AudioEngine.hasVideo && !root.presetPanelVisible
                 && SystemTheme.visualisationsEnabled
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        width: 22
        height: 64
        radius: Theme.radius
        color: presetHandleHover.hovered ? Theme.surfaceHigh : Theme.surface
        border.color: Theme.border
        border.width: 1
        Behavior on color { ColorAnimation { duration: 90 } }

        HoverHandler { id: presetHandleHover }
        TapHandler { onTapped: root.presetPanelVisible = true }

        IconButton {
            anchors.centerIn: parent
            glyph: "chevronRight"
            size: 20
            enabled: false          // the whole tab is the target; this is just the glyph
            opacity: 1.0
        }
        ToolTip.visible: presetHandleHover.hovered
        ToolTip.text: qsTr("Show visualisations")
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
