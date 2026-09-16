import QtQuick
import QtQuick.Controls

// Transport glyphs are drawn rather than shipped as icon assets: no icon theme dependency,
// and they stay crisp at any size on a software renderer.
AbstractButton {
    id: control

    property string glyph: "play"
    property bool primary: false
    property int size: primary ? Theme.controlSize : Theme.controlSizeSmall

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.35

    background: Rectangle {
        radius: width / 2
        color: control.primary
            ? (control.down ? Theme.accentDim : Theme.accent)
            : (control.hovered ? Theme.surfaceHigh : "transparent")
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Item {
        Canvas {
            id: canvas
            anchors.centerIn: parent
            width: control.size * 0.42
            height: control.size * 0.42
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = control.primary ? "#0f1115" : Theme.text
                const w = width, h = height
                switch (control.glyph) {
                case "play":
                    ctx.beginPath(); ctx.moveTo(w * 0.15, 0); ctx.lineTo(w, h / 2)
                    ctx.lineTo(w * 0.15, h); ctx.closePath(); ctx.fill(); break
                case "pause":
                    ctx.fillRect(w * 0.12, 0, w * 0.28, h)
                    ctx.fillRect(w * 0.60, 0, w * 0.28, h); break
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
