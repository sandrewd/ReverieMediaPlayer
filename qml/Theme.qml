pragma Singleton
import QtQuick

// One place for the visual language. "Simplicity of WMP, modern feel" is a direction, not a
// spec, so this is deliberately small: a dark neutral shell that lets the visualiser be the
// colourful thing on screen, one accent, and generous hit targets for the target user.
QtObject {
    readonly property color background:   "#0f1115"
    readonly property color surface:      "#171a21"
    readonly property color surfaceHigh:  "#1f242d"
    readonly property color border:       "#2a3039"
    readonly property color text:         "#e8eaed"
    readonly property color textDim:      "#9aa0a8"
    readonly property color accent:       "#4da3ff"
    readonly property color accentDim:    "#2e6ea8"

    readonly property int radius: 6
    readonly property int spacing: 12
    readonly property int controlSize: 44      // large hit targets, not 24px icons
    readonly property int controlSizeSmall: 34
    readonly property int minWindowWidth: 480
    readonly property int minWindowHeight: 320
}
