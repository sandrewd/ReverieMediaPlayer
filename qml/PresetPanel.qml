import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Player

// The visualisation browser, as a panel rather than a dialog.
//
// It replaced a cascading context menu that could not survive the full pack: 9,795 leaves across
// three levels, when 480 was already a measurable stall to build. A menu is the wrong instrument
// for a corpus; a virtualised list with a search box is the right one.
//
// It was a Dialog first, and that was wrong for a reason worth keeping: a QML dialog is clipped
// to the window, so it cannot be dragged aside, and it floats over the stage - which is the one
// thing you opened it to look at. Reported as feeling modal even though it was not. A panel
// shrinks the stage instead of covering it, mirrors the playlist on the other side, and needs no
// window management, which matters because a second top-level window inherits every Wayland
// decoration problem the brief records.
Rectangle {
    id: root

    property var visualizer: null
    signal collapseRequested()

    color: Theme.surface

    // Applying a preset narrows the rotation to exactly what is on screen, so "next" after
    // picking from a filtered list stays inside that filter. Choosing one deliberately locks
    // it, because the rotation would otherwise move off it within half a minute.
    function apply(row) {
        if (!visualizer || row < 0 || row >= PresetLibrary.filteredCount)
            return
        visualizer.setPresetList(PresetLibrary.filteredPaths())
        visualizer.jumpTo(row)
        visualizer.presetLocked = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: 40

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Visualisations")
                color: Theme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                glyph: "chevronLeft"
                size: 24
                onClicked: root.collapseRequested()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Hide visualisations")
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        TextField {
            id: searchField
            Layout.fillWidth: true
            Layout.margins: 8
            placeholderText: qsTr("Search…")
            text: PresetLibrary.searchText
            onTextEdited: PresetLibrary.searchText = text
            color: Theme.text
            background: Rectangle {
                implicitHeight: 30
                color: Theme.surfaceHigh
                border.color: searchField.activeFocus ? Theme.accent : Theme.border
                border.width: 1
                radius: 4
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 6

            ComboBox {
                id: categoryBox
                Layout.fillWidth: true
                // "All kinds" is a real entry rather than an empty one, or there is no way back
                // to the whole library once a category has been chosen.
                model: [qsTr("All kinds")].concat(PresetLibrary.categories)
                onActivated: {
                    PresetLibrary.filterCategory = currentIndex === 0 ? "" : currentText
                    // A style from the previous category does not exist in this one.
                    PresetLibrary.filterStyle = ""
                    styleBox.currentIndex = 0
                }
            }
            ComboBox {
                id: styleBox
                Layout.fillWidth: true
                // The pack's own second level - Glowsticks, Nested Spiral, Rorschach, Polar Warp.
                // Far more descriptive of what you see than the eleven categories above it.
                model: [qsTr("All styles")].concat(PresetLibrary.styles(PresetLibrary.filterCategory))
                enabled: model.length > 1
                onActivated: PresetLibrary.filterStyle = currentIndex === 0 ? "" : currentText
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            model: PresetLibrary
            clip: true
            // 9,795 rows is exactly what a ListView is for and exactly what a Menu is not.
            cacheBuffer: 200
            ScrollBar.vertical: ScrollBar {
                id: listScroll
                policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 10
            }

            delegate: ItemDelegate {
                id: row
                width: list.width - (listScroll.visible ? 12 : 0)
                height: 42
                highlighted: root.visualizer && root.visualizer.presetFile === model.path
                // isFavourite() is a plain call and creates no dependency on its own, so the
                // notifying property is named first: the comma evaluates both and QML binds to
                // the one it saw. Without it the stars never update - the same shape of trap as
                // a dynamically-named property lookup, which also silently is not a binding.
                readonly property bool isFavourite:
                    (PresetHistory.favourites, PresetHistory.isFavourite(model.path))
                onClicked: root.apply(model.index)
                ToolTip.visible: hovered
                ToolTip.text: model.name

                contentItem: RowLayout {
                    spacing: 4
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: row.highlighted ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            // Style first: it says what the thing looks like, which is what
                            // someone browsing is actually choosing by.
                            text: [model.style, model.author].filter(function (s) {
                                return s !== ""
                            }).join(" · ")
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    IconButton {
                        // Shown on hover, or whenever it is already a favourite - otherwise the
                        // only way to discover the control is to know it is there.
                        visible: row.hovered || row.isFavourite
                        glyph: row.isFavourite ? "starFilled" : "star"
                        size: 22
                        onClicked: PresetHistory.toggleFavourite(model.path)
                        ToolTip.visible: hovered
                        ToolTip.text: row.isFavourite ? qsTr("Remove from favourites")
                                                      : qsTr("Add to favourites")
                    }
                }
            }

            // An empty result is a state worth naming; a blank panel reads as broken.
            Text {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.pixelSize: 12
                text: PresetLibrary.count === 0 ? qsTr("No visualisations are installed.")
                                                : qsTr("Nothing matches that.")
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }

        Item {
            Layout.fillWidth: true
            implicitHeight: 38

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: PresetLibrary.filteredCount === PresetLibrary.count
                      ? qsTr("%1 visualisations").arg(PresetLibrary.count)
                      : qsTr("%1 of %2").arg(PresetLibrary.filteredCount).arg(PresetLibrary.count)
                color: Theme.textDim
                font.pixelSize: 11
            }
            Button {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Surprise me")
                enabled: PresetLibrary.filteredCount > 0
                // Random *within what is on screen*, so narrowing to a style and asking for a
                // random one does what it looks like it does.
                onClicked: root.apply(Math.floor(Math.random() * PresetLibrary.filteredCount))
            }
        }
    }
}
