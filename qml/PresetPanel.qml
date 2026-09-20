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
        if (!visualizer || AudioEngine.hasVideo
            || row < 0 || row >= PresetLibrary.filteredCount)
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

                // Its own ToolTip rather than the shared attached one, because this text is
                // several lines and the shared tooltip cannot be given a width. A Text with
                // wrapMode set reports an implicitWidth for the whole string *unwrapped* -
                // explicit newlines do not reduce it - so the stock tooltip came out 1086px wide
                // for 300px of text and covered the list it was describing. Measured, not
                // guessed: shortening the lines does nothing, only bounding the width does.
                //
                // Styled from the tokens directly, which is the rule for anything that has to
                // survive a theme change at runtime.
                HoverHandler { id: rowHover }

                ToolTip {
                    id: rowTip
                    parent: row
                    visible: row.hovered
                    // Placed against the pointer. Left to itself a Popup lands wherever its
                    // default placement puts it - measured at 393px to the right of the cursor
                    // on one row and 24px on another, which reads as belonging to nothing.
                    // Offset down and right so the pointer never covers the first line, and
                    // margins keep it inside the window near an edge.
                    x: rowHover.point.position.x + 14
                    y: rowHover.point.position.y + 18
                    margins: 6
                    // Long enough that brushing past a row on the way somewhere else does not
                    // raise it, short enough that pausing on one feels answered.
                    delay: 700
                    text: row.explain()
                    padding: 8
                    // The width goes on the popup, not on the Text. A Popup sizes itself to its
                    // contentItem's *implicitWidth*, and constraining the Text's width leaves
                    // that untouched - the box stayed 1086px wide with the text drawn 288px wide
                    // inside it. Measured both ways.
                    width: 304
                    contentItem: Text {
                        text: rowTip.text
                        font: rowTip.font
                        color: Theme.text
                        wrapMode: Text.WordWrap
                        width: rowTip.availableWidth
                    }
                    background: Rectangle {
                        color: Theme.surfaceHigh
                        border.color: Theme.border
                        border.width: 1
                        radius: 3
                    }
                }

                // Parsed from the preset only when someone hovers it. Reading all 9,795 files at
                // index time would cost far more than the whole index does.
                function explain() {
                    if (model.broken)
                        return qsTr("%1\n\nX  This visualisation renders nothing.\n"
                                    + "Measured over a six-second run \u2014 and supplying\n"
                                    + "the image it asks for does not help, so something\n"
                                    + "else is wrong with it.").arg(model.name)
                    const wanted = PresetLibrary.texturesWantedBy(model.path)
                    if (model.needsTexture) {
                        return qsTr("%1\n\n!  Renders nothing without an external image\n"
                                    + "that Reverie does not ship.\n\nIt wants: %2\n\nPut a "
                                    + "matching .png or .jpg in\n%3")
                               .arg(model.name)
                               .arg(wanted.join(", "))
                               .arg(PresetLibrary.textureDropPath)
                    }
                    if (model.usesTexture || wanted.length > 0) {
                        // No mark for these: they draw perfectly well, projectM stands a 1x1
                        // placeholder in for the missing image and the preset simply loses that
                        // layer. 1,783 of the pack are in this state and marking them all would
                        // teach people to ignore the mark. The information is still here for
                        // anyone assembling a texture pack.
                        return qsTr("%1\n\n\u25cf  An image this visualisation uses is not "
                                    + "installed, so part of it is missing.\n\nIt wants: %2\n\n"
                                    + "It still draws without it. Put a matching .png or .jpg "
                                    + "in\n%3")
                               .arg(model.name).arg(wanted.join(", "))
                               .arg(PresetLibrary.textureDropPath)
                    }
                    return model.name
                }

                contentItem: RowLayout {
                    spacing: 4
                    Item {
                        // Three levels, in order of severity: error, warning, informational.
                        //   X  renders nothing, and no image will fix it
                        //   !  renders nothing until an image it names is supplied
                        //   o  renders, but an image it asks for is missing from it
                        // The first two are plain ASCII - the Canvas-drawn icons elsewhere exist
                        // because a system font may not carry a given glyph, and "X" and "!" are
                        // in every font there is. The dot is a Rectangle for the same reason: a
                        // circle drawn as a circle depends on no font at all.
                        implicitWidth: 11
                        implicitHeight: 11
                        visible: model.broken || model.needsTexture || model.usesTexture
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: 2
                        Text {
                            anchors.centerIn: parent
                            visible: model.broken || model.needsTexture
                            text: model.broken ? "X" : "!"
                            color: model.broken ? Theme.alert : Theme.warn
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            visible: !model.broken && !model.needsTexture && model.usesTexture
                            width: 7
                            height: 7
                            radius: width / 2
                            color: Theme.info
                        }
                    }
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
