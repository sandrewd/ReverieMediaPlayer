import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Player

// The preset browser.
//
// This replaces a cascading context menu that could not survive the full pack: 9,795 leaves
// across three levels, when 480 was already a measurable stall to build. A menu is the wrong
// instrument for a corpus; a virtualised list with a search box is the right one.
//
// Deliberately NOT modal. You pick a visualisation in order to look at it, so a dialog that
// blocks the window hides the only thing being judged - the same reasoning that made the
// equaliser modeless.
Dialog {
    id: browser

    // The visualiser to drive. Set by the caller.
    property var visualizer: null

    title: qsTr("Visualisations")
    modal: false
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.NoButton

    // Offset rather than centred: the point of picking a preset is watching it, so leave as
    // much of the stage visible as the window allows.
    x: Math.max(12, parent ? parent.width - width - 24 : 12)
    y: Math.max(12, parent ? (parent.height - height) / 2 : 12)
    width: Math.min(420, parent ? parent.width - 48 : 420)
    height: Math.min(520, parent ? parent.height - 48 : 520)

    onOpened: {
        searchField.forceActiveFocus()
        categoryBox.syncFromLibrary()
    }
    // Leaving the browser should not leave the library filtered from underneath the menus.
    onClosed: PresetLibrary.clearFilters()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search visualisations…")
            text: PresetLibrary.searchText
            onTextEdited: PresetLibrary.searchText = text
            color: Theme.text
            background: Rectangle {
                implicitHeight: 32
                color: Theme.surfaceHigh
                border.color: searchField.activeFocus ? Theme.accent : Theme.border
                border.width: 1
                radius: 4
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ComboBox {
                id: categoryBox
                Layout.fillWidth: true
                // "Everything" has to be a real entry rather than an empty one, or there is no
                // way back to the whole library once a category has been chosen.
                model: [qsTr("All kinds")].concat(PresetLibrary.categories)
                function syncFromLibrary() {
                    currentIndex = PresetLibrary.filterCategory === ""
                            ? 0 : Math.max(0, model.indexOf(PresetLibrary.filterCategory))
                }
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
                // The pack's own second level - Glowsticks, Nested Spiral, Rorschach and so on.
                // Far more descriptive of what you see than the eleven categories above it.
                model: [qsTr("All styles")].concat(PresetLibrary.styles(PresetLibrary.filterCategory))
                enabled: model.length > 1
                onActivated: PresetLibrary.filterStyle = currentIndex === 0 ? "" : currentText
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceHigh
            border.color: Theme.border
            border.width: 1
            radius: 4
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                model: PresetLibrary
                clip: true
                // 9,795 rows is exactly what a ListView is for and exactly what a Menu is not.
                cacheBuffer: 200
                ScrollBar.vertical: ScrollBar {
                    policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn
                                                             : ScrollBar.AlwaysOff
                    width: 10
                }

                delegate: ItemDelegate {
                    width: list.width - (list.ScrollBar.vertical.visible ? 12 : 0)
                    height: 44
                    highlighted: browser.visualizer
                                 && browser.visualizer.presetName === model.name
                    onClicked: browser.apply(model.index)

                    contentItem: ColumnLayout {
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.text
                            font.pixelSize: 12
                            font.weight: parent.parent.highlighted ? Font.DemiBold : Font.Normal
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
                    text: PresetLibrary.count === 0
                          ? qsTr("No visualisations are installed.")
                          : qsTr("Nothing matches that.")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: PresetLibrary.filteredCount === PresetLibrary.count
                      ? qsTr("%1 visualisations").arg(PresetLibrary.count)
                      : qsTr("%1 of %2").arg(PresetLibrary.filteredCount).arg(PresetLibrary.count)
                color: Theme.textDim
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Surprise me")
                enabled: PresetLibrary.filteredCount > 0
                // Random *within what is on screen*, so narrowing to a style and asking for a
                // random one does what it looks like it does.
                onClicked: browser.apply(Math.floor(Math.random() * PresetLibrary.filteredCount))
            }
            Button {
                text: qsTr("Close")
                onClicked: browser.close()
            }
        }
    }

    // Choosing a specific visualisation locks it, because the rotation would otherwise move off
    // it within half a minute - the same reasoning the old picker used.
    function apply(row) {
        if (!visualizer || row < 0 || row >= PresetLibrary.filteredCount)
            return
        visualizer.setPresetList(PresetLibrary.filteredPaths())
        visualizer.jumpTo(row)
        visualizer.presetLocked = true
    }
}
