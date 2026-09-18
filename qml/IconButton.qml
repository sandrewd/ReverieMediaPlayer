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
    // A latched control - shuffle on, repeat set. Tinted rather than filled, so it reads as
    // "this mode is on" without competing with the play button.
    property bool active: false
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
            // A primary button is filled with the accent, which the user can now choose, so
            // the glyph has to be picked against it rather than assumed dark.
            property color glyphColor: control.primary
                ? (Theme.accent.hslLightness > 0.55 ? "#0f1115" : "#f2f4f6")
                : control.active ? Theme.accent
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
                case "shuffle":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.10)
                    ctx.beginPath()
                    ctx.moveTo(w * 0.08, h * 0.28); ctx.lineTo(w * 0.34, h * 0.28)
                    ctx.lineTo(w * 0.66, h * 0.72); ctx.lineTo(w * 0.88, h * 0.72)
                    ctx.moveTo(w * 0.08, h * 0.72); ctx.lineTo(w * 0.34, h * 0.72)
                    ctx.lineTo(w * 0.66, h * 0.28); ctx.lineTo(w * 0.88, h * 0.28)
                    ctx.stroke()
                    // Arrow heads, so it reads as crossing paths rather than a letter.
                    ctx.beginPath()
                    ctx.moveTo(w * 0.92, h * 0.28); ctx.lineTo(w * 0.74, h * 0.18)
                    ctx.lineTo(w * 0.74, h * 0.38); ctx.closePath(); ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo(w * 0.92, h * 0.72); ctx.lineTo(w * 0.74, h * 0.62)
                    ctx.lineTo(w * 0.74, h * 0.82); ctx.closePath(); ctx.fill()
                    break
                case "repeat":
                case "repeatOne":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.10)
                    ctx.beginPath()
                    ctx.moveTo(w * 0.24, h * 0.22); ctx.lineTo(w * 0.72, h * 0.22)
                    ctx.arcTo(w * 0.92, h * 0.22, w * 0.92, h * 0.50, w * 0.20)
                    ctx.moveTo(w * 0.76, h * 0.78); ctx.lineTo(w * 0.28, h * 0.78)
                    ctx.arcTo(w * 0.08, h * 0.78, w * 0.08, h * 0.50, w * 0.20)
                    ctx.stroke()
                    ctx.beginPath()
                    ctx.moveTo(w * 0.20, h * 0.10); ctx.lineTo(w * 0.20, h * 0.34)
                    ctx.lineTo(w * 0.04, h * 0.22); ctx.closePath(); ctx.fill()
                    ctx.beginPath()
                    ctx.moveTo(w * 0.80, h * 0.90); ctx.lineTo(w * 0.80, h * 0.66)
                    ctx.lineTo(w * 0.96, h * 0.78); ctx.closePath(); ctx.fill()
                    if (control.glyph === "repeatOne") {
                        // A "1" in the middle: the only difference between the two modes, so it
                        // has to be legible at 34px.
                        ctx.lineWidth = Math.max(1, w * 0.09)
                        ctx.beginPath()
                        ctx.moveTo(w * 0.42, h * 0.44); ctx.lineTo(w * 0.52, h * 0.38)
                        ctx.lineTo(w * 0.52, h * 0.64)
                        ctx.stroke()
                    }
                    break
                case "random":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.09)
                    ctx.strokeRect(w * 0.08, h * 0.08, w * 0.84, h * 0.84)
                    ctx.beginPath(); ctx.arc(w * 0.30, h * 0.30, w * 0.08, 0, 6.3); ctx.fill()
                    ctx.beginPath(); ctx.arc(w * 0.70, h * 0.70, w * 0.08, 0, 6.3); ctx.fill()
                    ctx.beginPath(); ctx.arc(w * 0.50, h * 0.50, w * 0.08, 0, 6.3); ctx.fill()
                    break
                case "subtitles":
                    // The conventional captions mark: an outlined frame with two short rules
                    // inside it. Drawn rather than set as a glyph character for the reason the
                    // play/pause mark is - it must not depend on what the system font carries.
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.09)
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.rect(w * 0.06, h * 0.18, w * 0.88, h * 0.64)
                    ctx.stroke()
                    ctx.lineCap = "round"
                    ctx.lineWidth = Math.max(1, w * 0.11)
                    ctx.beginPath()
                    ctx.moveTo(w * 0.20, h * 0.60); ctx.lineTo(w * 0.46, h * 0.60)
                    ctx.moveTo(w * 0.56, h * 0.60); ctx.lineTo(w * 0.80, h * 0.60)
                    ctx.stroke(); break
                case "expand":
                    ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Math.max(1, w * 0.12)
                    ctx.beginPath()
                    ctx.moveTo(w * 0.05, h * 0.35); ctx.lineTo(w * 0.05, h * 0.05); ctx.lineTo(w * 0.35, h * 0.05)
                    ctx.moveTo(w * 0.65, h * 0.05); ctx.lineTo(w * 0.95, h * 0.05); ctx.lineTo(w * 0.95, h * 0.35)
                    ctx.moveTo(w * 0.95, h * 0.65); ctx.lineTo(w * 0.95, h * 0.95); ctx.lineTo(w * 0.65, h * 0.95)
                    ctx.moveTo(w * 0.35, h * 0.95); ctx.lineTo(w * 0.05, h * 0.95); ctx.lineTo(w * 0.05, h * 0.65)
                    ctx.stroke(); break
                case "star":
                case "starFilled":
                    // Drawn rather than set as a glyph character, like every other icon here:
                    // a star from the system font is not guaranteed to exist and not guaranteed
                    // to be the same shape twice.
                    ctx.strokeStyle = ctx.fillStyle
                    ctx.lineWidth = Math.max(1, w * 0.10)
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    for (let k = 0; k < 10; ++k) {
                        const r = (k % 2 === 0) ? w * 0.44 : w * 0.18
                        const a = -Math.PI / 2 + k * Math.PI / 5
                        const px = w * 0.5 + r * Math.cos(a)
                        const py = h * 0.5 + r * Math.sin(a)
                        if (k === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                    }
                    ctx.closePath()
                    if (control.glyph === "starFilled") ctx.fill(); else ctx.stroke()
                    break
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
