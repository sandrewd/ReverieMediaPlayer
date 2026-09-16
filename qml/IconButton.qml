import QtQuick
import QtQuick.Controls

// Transport glyphs are drawn rather than shipped as icon assets: no icon theme dependency,
// and they stay crisp at any size on a software renderer.
AbstractButton {
    id: control

    property string glyph: "play"
    property bool primary: false
    // Buttons sitting over the visualiser cannot borrow the shell's text colour: whatever the
    // theme, the thing behind them is arbitrary video and may be any brightness.
    property bool overVideo: false
    property int size: primary ? Theme.controlSize : Theme.controlSizeSmall

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.35

    background: Rectangle {
        radius: width / 2
        color: control.primary
            ? (control.down ? Theme.accentDim : Theme.accent)
            : control.overVideo
                ? (control.hovered ? "#66ffffff" : "transparent")
                : (control.hovered ? Theme.surfaceHigh : "transparent")
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Item {
        Canvas {
            id: canvas
            anchors.centerIn: parent
            width: control.size * 0.42
            height: control.size * 0.42

            // Repaint when the colour changes, not just the glyph: the theme can now switch
            // while the application is running.
            property color glyphColor: control.primary ? "#0f1115"
                                     : control.overVideo ? Theme.overlayText : Theme.text
            onGlyphColorChanged: canvas.requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = canvas.glyphColor
                const w = width, h = height
                switch (control.glyph) {
                case "play":
                    ctx.beginPath(); ctx.moveTo(w * 0.15, 0); ctx.lineTo(w, h / 2)
                    ctx.lineTo(w * 0.15, h); ctx.closePath(); ctx.fill(); break
                case "pause":
                    ctx.fillRect(w * 0.12, 0, w * 0.28, h)
                    ctx.fillRect(w * 0.60, 0, w * 0.28, h); break
                case "stop":
                    ctx.fillRect(w * 0.08, h * 0.08, w * 0.84, h * 0.84); break
                case "next":
                    ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(w * 0.7, h / 2)
                    ctx.lineTo(0, h); ctx.closePath(); ctx.fill()
                    ctx.fillRect(w * 0.78, 0, w * 0.18, h); break
                case "prev":
                    ctx.beginPath(); ctx.moveTo(w, 0); ctx.lineTo(w * 0.3, h / 2)
                    ctx.lineTo(w, h); ctx.closePath(); ctx.fill()
                    ctx.fillRect(w * 0.04, 0, w * 0.18, h); break
                case "volume":
                    ctx.beginPath(); ctx.moveTo(0, h * 0.32); ctx.lineTo(w * 0.28, h * 0.32)
                    ctx.lineTo(w * 0.58, 0); ctx.lineTo(w * 0.58, h)
                    ctx.lineTo(w * 0.28, h * 0.68); ctx.lineTo(0, h * 0.68)
                    ctx.closePath(); ctx.fill()
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.08)
                    ctx.beginPath(); ctx.arc(w * 0.62, h / 2, w * 0.28, -0.9, 0.9); ctx.stroke()
                    break
                case "muted":
                    ctx.beginPath(); ctx.moveTo(0, h * 0.32); ctx.lineTo(w * 0.28, h * 0.32)
                    ctx.lineTo(w * 0.58, 0); ctx.lineTo(w * 0.58, h)
                    ctx.lineTo(w * 0.28, h * 0.68); ctx.lineTo(0, h * 0.68)
                    ctx.closePath(); ctx.fill()
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.1)
                    ctx.beginPath(); ctx.moveTo(w * 0.70, h * 0.30); ctx.lineTo(w * 0.98, h * 0.70)
                    ctx.moveTo(w * 0.98, h * 0.30); ctx.lineTo(w * 0.70, h * 0.70); ctx.stroke()
                    break
                case "list":
                    for (let i = 0; i < 3; ++i) {
                        ctx.fillRect(0, h * (0.1 + i * 0.36), w * 0.22, h * 0.16)
                        ctx.fillRect(w * 0.34, h * (0.1 + i * 0.36), w * 0.66, h * 0.16)
                    }
                    break
                case "mini":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.1)
                    ctx.strokeRect(w * 0.05, h * 0.05, w * 0.9, h * 0.9)
                    ctx.fillRect(w * 0.05, h * 0.62, w * 0.9, h * 0.33); break
                case "lock":
                    ctx.fillRect(w * 0.12, h * 0.45, w * 0.76, h * 0.55)
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.12)
                    ctx.beginPath(); ctx.arc(w / 2, h * 0.42, w * 0.24, Math.PI, 0); ctx.stroke()
                    break
                case "unlock":
                    ctx.fillRect(w * 0.12, h * 0.45, w * 0.76, h * 0.55)
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.12)
                    ctx.beginPath(); ctx.arc(w * 0.74, h * 0.42, w * 0.24, Math.PI, 0); ctx.stroke()
                    break
                case "random":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.09)
                    ctx.strokeRect(w * 0.08, h * 0.08, w * 0.84, h * 0.84)
                    ctx.beginPath(); ctx.arc(w * 0.30, h * 0.30, w * 0.08, 0, 6.3); ctx.fill()
                    ctx.beginPath(); ctx.arc(w * 0.70, h * 0.70, w * 0.08, 0, 6.3); ctx.fill()
                    ctx.beginPath(); ctx.arc(w * 0.50, h * 0.50, w * 0.08, 0, 6.3); ctx.fill()
                    break
                case "expand":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.12)
                    ctx.beginPath()
                    ctx.moveTo(w * 0.05, h * 0.35); ctx.lineTo(w * 0.05, h * 0.05); ctx.lineTo(w * 0.35, h * 0.05)
                    ctx.moveTo(w * 0.65, h * 0.05); ctx.lineTo(w * 0.95, h * 0.05); ctx.lineTo(w * 0.95, h * 0.35)
                    ctx.moveTo(w * 0.95, h * 0.65); ctx.lineTo(w * 0.95, h * 0.95); ctx.lineTo(w * 0.65, h * 0.95)
                    ctx.moveTo(w * 0.35, h * 0.95); ctx.lineTo(w * 0.05, h * 0.95); ctx.lineTo(w * 0.05, h * 0.65)
                    ctx.stroke(); break
                case "chevronRight":
                    ctx.strokeStyle = ctx.fillStyle
                    ctx.lineWidth = Math.max(1.5, w * 0.16)
                    ctx.lineCap = "round"; ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(w * 0.34, h * 0.12)
                    ctx.lineTo(w * 0.72, h * 0.5)
                    ctx.lineTo(w * 0.34, h * 0.88)
                    ctx.stroke(); break
                case "chevronLeft":
                    ctx.strokeStyle = ctx.fillStyle
                    ctx.lineWidth = Math.max(1.5, w * 0.16)
                    ctx.lineCap = "round"; ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(w * 0.66, h * 0.12)
                    ctx.lineTo(w * 0.28, h * 0.5)
                    ctx.lineTo(w * 0.66, h * 0.88)
                    ctx.stroke(); break
                case "menu":
                    for (let j = 0; j < 3; ++j)
                        ctx.fillRect(0, h * (0.1 + j * 0.36), w, h * 0.16)
                    break
                }
            }
            Connections {
                target: control
                // `parent` inside Connections is not the Canvas, so name it explicitly.
                function onGlyphChanged() { canvas.requestPaint() }
            }
        }
    }
}
