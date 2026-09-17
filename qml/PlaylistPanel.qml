import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Player

Rectangle {
    id: root

    signal playRequested(int index)
    signal collapseRequested()
    signal addStreamRequested()

    color: Theme.surface

    // Multi-select without a selection model: the view tracks it, the model stays a plain list.
    property var selection: ({})
    property int lastClicked: -1

    function isSelected(i) { return selection[i] === true }
    function clearSelection() { selection = ({}); selectionChanged() }
    function toggle(i) {
        const s = selection; s[i] = !s[i]; if (!s[i]) delete s[i]
        selection = s; selectionChanged()
    }
    function selectOnly(i) { selection = ({}); selection[i] = true; selectionChanged() }
    function selectRange(from, to) {
        const s = ({})
        for (let i = Math.min(from, to); i <= Math.max(from, to); ++i) s[i] = true
        selection = s; selectionChanged()
    }
    function selectedRows() {
        const rows = []
        for (const key in selection) if (selection[key]) rows.push(parseInt(key))
        return rows.sort((a, b) => a - b)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Anchored rather than laid out. A RowLayout with Layout.margins came out 344 wide
        // inside a 340 panel, which pushed the track count past the edge and clipped it.
        // Anchoring the count to the right edge means it cannot overflow whatever the width.
        Item {
            Layout.fillWidth: true
            implicitHeight: 40

            IconButton {
                id: collapseButton
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                glyph: "chevronRight"
                size: 24
                onClicked: root.collapseRequested()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Hide playlist")
            }

            Label {
                id: countLabel
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: PlaylistModel.count + (PlaylistModel.count === 1 ? qsTr(" track")
                                                                       : qsTr(" tracks"))
                color: Theme.textDim
                font.pixelSize: 11
            }

            Label {
                anchors.left: collapseButton.right
                anchors.leftMargin: 6
                anchors.right: countLabel.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Playlist")
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: PlaylistModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            // Drag reorder: the dragged delegate floats, and the row under the cursor swaps.
            property int draggingIndex: -1

            delegate: Rectangle {
                id: row
                width: list.width
                height: 40
                color: model.isCurrent ? Theme.accentDim
                     : root.isSelected(index) ? Theme.surfaceHigh
                     : (hover.hovered ? Qt.lighter(Theme.surface, 1.35) : "transparent")
                Behavior on color { ColorAnimation { duration: 80 } }

                HoverHandler { id: hover }

                // Titles elide, and a title derived from tags no longer resembles the filename at
                // all, so hovering is the only way back to what is actually on disk.
                ToolTip.visible: hover.hovered && model.fileName !== ""
                ToolTip.text: model.fileName
                ToolTip.delay: 600

                // The background tint alone reads as "selected" and depends on whatever the
                // user's highlight colour happens to be. A hard edge marks the playing track
                // unambiguously in every palette.
                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 3
                    color: Theme.accent
                    visible: model.isCurrent
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Item {
                        Layout.preferredWidth: 26
                        Layout.fillHeight: true

                        Label {
                            anchors.centerIn: parent
                            visible: !model.isCurrent
                            text: index + 1
                            color: Theme.textDim
                            font.pixelSize: 11
                            font.family: "monospace"
                        }

                        // Drawn rather than a glyph character: no dependency on which symbols
                        // the system font happens to carry.
                        Canvas {
                            id: nowPlayingMark
                            anchors.centerIn: parent
                            width: 11; height: 11
                            visible: model.isCurrent
                            property bool paused: AudioEngine.state !== AudioEngine.Playing
                            // Contrast is against the row's own highlighted background, so it
                            // follows the text colour rather than the accent - an accent mark
                            // on an accent-derived row can disappear in some palettes.
                            property color mark: Theme.text
                            onPausedChanged: requestPaint()
                            onMarkChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = mark
                                if (paused) {
                                    ctx.fillRect(0, 0, width * 0.34, height)
                                    ctx.fillRect(width * 0.62, 0, width * 0.34, height)
                                } else {
                                    ctx.beginPath()
                                    ctx.moveTo(0, 0)
                                    ctx.lineTo(width, height / 2)
                                    ctx.lineTo(0, height)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: model.isCurrent ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: model.artist !== ""
                            text: model.artist
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        visible: model.isStream
                        implicitWidth: streamTag.implicitWidth + 10
                        implicitHeight: streamTag.implicitHeight + 4
                        radius: 3
                        color: "transparent"
                        border.color: Theme.textDim
                        border.width: 1
                        Label {
                            id: streamTag
                            anchors.centerIn: parent
                            text: qsTr("LIVE")
                            color: Theme.textDim
                            font.pixelSize: 8
                            font.letterSpacing: 0.8
                        }
                    }
                    Label {
                        visible: !model.isStream
                        // The playing track counts down, which says "this one is running" in a
                        // way a static duration never can.
                        text: {
                            if (!model.isCurrent || AudioEngine.duration <= 0)
                                return model.durationText
                            const left = Math.max(0, Math.round(
                                (AudioEngine.duration - AudioEngine.position) / 1000))
                            const m = Math.floor(left / 60)
                            const sec = left % 60
                            return "-" + m + ":" + (sec < 10 ? "0" : "") + sec
                        }
                        color: model.isCurrent ? Theme.text : Theme.textDim
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: function(event) {
                        if (event.modifiers & Qt.ShiftModifier && root.lastClicked >= 0)
                            root.selectRange(root.lastClicked, index)
                        else if (event.modifiers & Qt.ControlModifier)
                            root.toggle(index)
                        else
                            root.selectOnly(index)
                        root.lastClicked = index
                    }
                    onDoubleTapped: root.playRequested(index)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        if (!root.isSelected(index)) {
                            root.selectOnly(index)
                            root.lastClicked = index
                        }
                        contextMenu.popup()
                    }
                }

                DragHandler {
                    id: drag
                    yAxis.enabled: true
                    xAxis.enabled: false
                    onActiveChanged: {
                        if (active) {
                            list.draggingIndex = index
                        } else if (list.draggingIndex >= 0) {
                            const target = Math.max(0, Math.min(PlaylistModel.count - 1,
                                Math.round((row.y + row.height / 2) / row.height)))
                            if (target !== list.draggingIndex)
                                PlaylistModel.moveRow(list.draggingIndex, target)
                            list.draggingIndex = -1
                            root.clearSelection()
                        }
                    }
                }
                z: drag.active ? 2 : 1
                opacity: drag.active ? 0.85 : 1.0
            }

            // Empty state: the target user should never face a blank rectangle.
            Label {
                anchors.centerIn: parent
                visible: PlaylistModel.count === 0
                text: qsTr("No tracks yet.\nUse Add files or drop a folder in.")
                color: Theme.textDim
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        // One "Add" button rather than three. Three of them made this row demand ~344px, and
        // a ColumnLayout will not lay out narrower than its widest child's minimum width, so
        // every row in the panel - including the header - was being given 344px inside a
        // 340px panel and clipped. It is also simply less to read.
        Item {
            Layout.fillWidth: true
            implicitHeight: 42

            Button {
                id: addButton
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Add")
                flat: true
                onClicked: addMenu.popup()
            }

            Button {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Clear")
                flat: true
                enabled: PlaylistModel.count > 0
                onClicked: { PlaylistModel.clear(); root.clearSelection() }
            }
        }
    }

    Menu {
        id: addMenu
        MenuItem {
            text: qsTr("Files…")
            onTriggered: fileDialog.open()
        }
        MenuItem {
            text: qsTr("Folder…")
            onTriggered: folderDialog.open()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Radio stream…")
            onTriggered: root.addStreamRequested()
        }
    }

    Menu {
        id: contextMenu
        onAboutToShow: {
            let widest = 0
            for (let i = 0; i < count; ++i) {
                const item = itemAt(i)
                if (item && item.implicitWidth > widest)
                    widest = item.implicitWidth
            }
            width = Math.max(160, Math.min(360, widest + 44))
        }
        MenuItem {
            text: qsTr("Play")
            enabled: root.selectedRows().length === 1
            onTriggered: root.playRequested(root.selectedRows()[0])
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Move up")
            enabled: root.selectedRows().length === 1 && root.selectedRows()[0] > 0
            onTriggered: {
                const i = root.selectedRows()[0]
                PlaylistModel.moveRow(i, i - 1)
                root.selectOnly(i - 1)
            }
        }
        MenuItem {
            text: qsTr("Move down")
            enabled: root.selectedRows().length === 1
                     && root.selectedRows()[0] < PlaylistModel.count - 1
            onTriggered: {
                const i = root.selectedRows()[0]
                PlaylistModel.moveRow(i, i + 1)
                root.selectOnly(i + 1)
            }
        }
        MenuSeparator {}
        MenuItem {
            text: root.selectedRows().length > 1
                  ? qsTr("Remove %1 tracks").arg(root.selectedRows().length)
                  : qsTr("Remove")
            onTriggered: {
                PlaylistModel.removeRowsAt(root.selectedRows())
                root.clearSelection()
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Add files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Media files (*.mp3 *.flac *.ogg *.oga *.opus *.m4a *.aac *.wav *.wma *.aiff *.mp4 *.m4v *.webm *.mkv *.avi *.mov *.ogv *.wmv *.flv *.mpg *.mpeg *.ts)"),
                      qsTr("Audio only (*.mp3 *.flac *.ogg *.oga *.opus *.m4a *.aac *.wav *.wma *.aiff)"),
                      qsTr("Video only (*.mp4 *.m4v *.webm *.mkv *.avi *.mov *.ogv *.wmv *.flv *.mpg *.mpeg *.ts)"),
                      qsTr("All files (*)")]
        onAccepted: PlaylistModel.addFiles(selectedFiles)
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Add folder")
        onAccepted: PlaylistModel.addFolder(selectedFolder)
    }
}
