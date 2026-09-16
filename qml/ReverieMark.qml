import QtQuick
import QtQuick.Shapes

// The Reverie mark: a 3:2 Lissajous figure, the shape audio makes on an XY oscilloscope.
//
// Drawn parametrically rather than loaded from brand/reverie-mark.svg on purpose. That file
// uses stroke="currentColor", and Qt's SVG renderer implements a subset of SVG 1.2 Tiny which
// does not resolve it — an Image would come out unthemed. Generating the path here also binds
// strokeColor straight to the palette, so the in-app mark follows the user's accent live,
// stays resolution-independent, and could animate a draw-on later via strokeDashOffset.
//
// The launcher icon is a separate, fixed-colour asset and must never be themed this way.
Item {
    id: root

    // Master grid is 64 units: centre (32,32), radius 23, stroke 5.0.
    property color color: Theme.accent
    property real strokeScale: 1.0
    readonly property real unit: Math.min(width, height) / 64

    implicitWidth: 64
    implicitHeight: 64

    Shape {
        anchors.fill: parent
        antialiasing: true
        // Curve quality matters more than fill here; the mark is a single open stroke.
        ShapePath {
            strokeColor: root.color
            strokeWidth: Math.max(1, 5.0 * root.unit * root.strokeScale)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathPolyline {
                path: {
                    const points = []
                    const u = root.unit
                    const cx = 32 * u, cy = 32 * u, r = 23 * u
                    const segments = 240
                    for (let i = 0; i <= segments; ++i) {
                        const t = (i / segments) * 2 * Math.PI
                        points.push(Qt.point(cx + r * Math.sin(3 * t),
                                             cy + r * Math.sin(2 * t + Math.PI / 2)))
                    }
                    return points
                }
            }
        }
    }
}
