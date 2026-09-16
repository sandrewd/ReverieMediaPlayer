import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Player

Rectangle {
    id: root

    signal playRequested(int index)

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

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6

            Label {
                text: qsTr("Playlist")
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: PlaylistModel.count + (PlaylistModel.count === 1 ? qsTr(" track") : qsTr(" tracks"))
                color: Theme.textDim
                font.pixelSize: 11
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

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Label {
                        text: index + 1
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.family: "monospace"
                        Layout.preferredWidth: 26
                        horizontalAlignment: Text.AlignRight
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.text
                            font.pixelSize: 12
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
                    Label {
                        text: model.durationText
                        color: Theme.textDim
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

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6

            Button {
                text: qsTr("Add files")
                onClicked: fileDialog.open()
                flat: true
            }
            Button {
                text: qsTr("Add folder")
                onClicked: folderDialog.open()
                flat: true
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Clear")
                enabled: PlaylistModel.count > 0
                flat: true
                onClicked: { PlaylistModel.clear(); root.clearSelection() }
            }
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
        nameFilters: [qsTr("Audio files (*.mp3 *.flac *.ogg *.oga *.opus *.m4a *.aac *.wav *.wma *.aiff)"),
                      qsTr("All files (*)")]
        onAccepted: PlaylistModel.addFiles(selectedFiles)
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Add folder")
        onAccepted: PlaylistModel.addFolder(selectedFolder)
    }
}
