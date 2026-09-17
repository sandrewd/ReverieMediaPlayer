pragma Singleton
import QtQuick
import Player

// One place for the visual language. Dark was never specified, it was assumed, so the palette
// follows the desktop, can be forced either way, or replaced entirely by the user.
//
// Only four colours are user-settable. Everything else is derived from them, which means a
// custom theme cannot end up with invisible text or a border that vanishes into its panel.
QtObject {
    readonly property bool custom: SystemTheme.preference === SystemTheme.Custom
    readonly property bool dark: SystemTheme.dark

    readonly property color background: custom ? SystemTheme.customBackground
                                               : (dark ? "#0f1115" : "#f2f3f5")
    readonly property color surface:    custom ? SystemTheme.customSurface
                                               : (dark ? "#171a21" : "#ffffff")
    readonly property color text:       custom ? SystemTheme.customText
                                               : (dark ? "#e8eaed" : "#1b1e23")
    readonly property color accent:     custom ? SystemTheme.customAccent
                                               : (dark ? "#4da3ff" : "#1f6feb")
    // The visualiser stage. Dark in both schemes on purpose - what sits on it is video or a
    // Milkdrop preset, which read best against dark - but not pure black, because a black stage
    // on a black desktop leaves the window with no visible edge.
    readonly property color stage:      custom ? SystemTheme.customStage
                                               : (dark ? "#08090c" : "#1b1e23")
    // 0 means the visualiser is untouched while it runs; the stage colour is then only what you
    // see when nothing is playing.
    readonly property real stageTint:   SystemTheme.stageTint / 100.0

    // Derived. Nudging towards or away from the surface keeps hover states and borders
    // visible whichever direction the user's theme leans.
    readonly property color surfaceHigh: dark ? Qt.lighter(surface, 1.45) : Qt.darker(surface, 1.07)
    readonly property color border:      dark ? Qt.lighter(surface, 2.0)  : Qt.darker(surface, 1.16)
    readonly property color textDim:     Qt.rgba(text.r, text.g, text.b, 0.62)
    readonly property color accentDim:   dark ? Qt.darker(accent, 1.7) : Qt.lighter(accent, 1.45)

    // Text drawn over the visualiser is always light: the video behind it is dark-ish in every
    // theme, so following the shell here would make it unreadable.
    // The transport's top edge is a special case: whatever is above it is always the stage -
    // video or a preset - and therefore always dark. A border derived from the panel colour
    // disappears there in a dark theme, which makes a windowed transport look as though it is
    // covering the picture. This one contrasts with the bar, so the boundary reads in both.
    readonly property color stageEdge: dark ? Qt.rgba(1, 1, 1, 0.26) : border

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
