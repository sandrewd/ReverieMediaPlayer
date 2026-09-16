pragma Singleton
import QtQuick
import Player

// One place for the visual language. Dark was never specified, it was assumed, so the palette
// now follows the desktop and can be overridden. The visualiser is the colourful thing on
// screen in either mode; the shell stays neutral around it.
QtObject {
    readonly property bool dark: SystemTheme.dark

    readonly property color background:  dark ? "#0f1115" : "#f2f3f5"
    readonly property color surface:     dark ? "#171a21" : "#ffffff"
    readonly property color surfaceHigh: dark ? "#1f242d" : "#e7eaee"
    readonly property color border:      dark ? "#2a3039" : "#d3d8de"
    readonly property color text:        dark ? "#e8eaed" : "#1b1e23"
    readonly property color textDim:     dark ? "#9aa0a8" : "#5c636d"
    readonly property color accent:      dark ? "#4da3ff" : "#1f6feb"
    readonly property color accentDim:   dark ? "#2e6ea8" : "#9dc4f5"

    // Text drawn over the visualiser is always light: the video behind it is dark-ish in
    // both themes, so following the shell here would make it unreadable in light mode.
    readonly property color overlayText: "#f0f2f4"
    readonly property color overlayTextDim: "#a8b0b8"
    readonly property color overlayBackground: "#b0000000"

    readonly property int radius: 6
    readonly property int spacing: 12
    readonly property int controlSize: 44      // large hit targets, not 24px icons
    readonly property int controlSizeSmall: 34
    readonly property int minWindowWidth: 480
    readonly property int minWindowHeight: 320
}
