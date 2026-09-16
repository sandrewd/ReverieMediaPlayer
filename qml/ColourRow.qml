import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// One editable colour. Deliberately takes its value as a plain property and reports changes
// through a signal, rather than reaching into a named property on a singleton: QML does not
// create a binding dependency for a dynamically-named lookup, so a swatch written that way
// never repaints and the whole row looks dead.
RowLayout {
    id: root

    property string label: ""
    property color colour: "black"
    signal colourPicked(color picked)

    spacing: 8

    Label {
        text: root.label
        color: Theme.text
        font.pixelSize: 12
        Layout.preferredWidth: 92
    }

    // The swatch opens the platform chooser, which brings its own palette.
    Rectangle {
        implicitWidth: 44
        implicitHeight: 26
        radius: 4
        color: root.colour
        border.color: Theme.border
        border.width: 1
        TapHandler {
            onTapped: {
                picker.selectedColor = root.colour
                picker.open()
            }
        }
    }

    // ...and the field beside it takes a value typed straight in, so neither route depends
    // on the other being any good.
    TextField {
        id: field
        Layout.fillWidth: true
        font.family: "monospace"
        font.pixelSize: 12
        selectByMouse: true
        // Styled from the tokens rather than the palette: an item created before the
        // application palette changed keeps its resolved palette in Qt 6.4, which left these
        // fields stark white inside a dark dialog.
        color: Theme.text
        placeholderTextColor: Theme.textDim
        background: Rectangle {
            color: Theme.surfaceHigh
            border.color: field.activeFocus ? Theme.accent : Theme.border
            border.width: 1
            radius: 3
        }
        // Rebinds whenever the colour changes from anywhere, including the picker.
        text: root.colour.toString().toUpperCase()
        onEditingFinished: {
            const value = text.trim()
            if (/^#[0-9A-Fa-f]{6}$/.test(value))
                root.colourPicked(value)
            else
                text = Qt.binding(function() { return root.colour.toString().toUpperCase() })
        }
    }

    ColorDialog {
        id: picker
        title: qsTr("Choose %1").arg(root.label.toLowerCase())
        onAccepted: root.colourPicked(selectedColor)
    }
}
