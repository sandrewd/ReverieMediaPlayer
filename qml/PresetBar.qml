import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Preset navigation. Kept to four controls on purpose: the corpus is thousands of presets and
// the target user does not want a browser, they want "not this one" and "keep this one".
Rectangle {
    id: root

    property var visualizer: null
    property bool showQuality: false

    implicitHeight: 44
    implicitWidth: row.implicitWidth + 24
    radius: Theme.radius
    color: Theme.overlayBackground

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 2

        IconButton {
            glyph: "prev"
            size: 30
            enabled: root.visualizer && root.visualizer.presetCount > 0
            onClicked: root.visualizer.previousPreset()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Previous preset")
        }
        IconButton {
            glyph: "random"
            size: 30
            enabled: root.visualizer && root.visualizer.presetCount > 0
            onClicked: root.visualizer.randomPreset()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Random preset")
        }
        IconButton {
            glyph: "next"
            size: 30
            enabled: root.visualizer && root.visualizer.presetCount > 0
            onClicked: root.visualizer.nextPreset()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Next preset")
        }
        IconButton {
            glyph: root.visualizer && root.visualizer.presetLocked ? "lock" : "unlock"
            size: 30
            onClicked: root.visualizer.presetLocked = !root.visualizer.presetLocked
            ToolTip.visible: hovered
            ToolTip.text: root.visualizer && root.visualizer.presetLocked
                          ? qsTr("Preset locked — it will not change on its own")
                          : qsTr("Lock this preset")
        }

        Item { width: 6; height: 1 }

        ColumnLayout {
            spacing: 0
            Layout.maximumWidth: 260
            // Labelled explicitly: without this the preset name reads as track information,
            // which is exactly how it was misread the first time someone ran the app.
            Label {
                text: qsTr("VISUALISATION")
                color: Theme.overlayTextDim
                font.pixelSize: 8
                font.letterSpacing: 1.2
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                text: root.visualizer && root.visualizer.presetName !== ""
                      ? root.visualizer.presetName : qsTr("No preset library")
                color: Theme.overlayText
                font.pixelSize: 11
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: root.visualizer && root.visualizer.presetCount > 0
                text: root.visualizer
                      ? qsTr("%1 of %2%3").arg(root.visualizer.presetIndex + 1)
                            .arg(root.visualizer.presetCount)
                            .arg(root.showQuality
                                 ? " · " + Math.round(root.visualizer.renderScale * 100) + "%"
                                   + (root.visualizer.adaptiveQuality ? qsTr(" auto") : "")
                                 : "")
                      : ""
                color: Theme.overlayTextDim
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
    }
}
