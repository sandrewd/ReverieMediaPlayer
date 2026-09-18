import QtQuick

// The Reverie mark, rendered from the shipped gradient master.
//
// Deliberately NOT themed. BRANDING.md §4 proposed that the in-app mark follow the user's
// accent; that was overruled — the mark is identity, and identity should not change with the
// user's colour choices. Keeping it static also means the in-app mark and the launcher icon
// are provably the same artwork rather than two things kept in agreement by hand.
//
// Because it no longer needs a live colour binding there is no reason to redraw the curve
// parametrically: the scalable icon carries a real linearGradient — not currentColor, which
// Qt's SVG renderer would refuse to resolve — so it renders correctly as an ordinary Image.
Item {
    id: root

    // An Item wrapper, because Image takes its implicit size from its source and will not
    // accept one being set.
    implicitWidth: 64
    implicitHeight: 64

    Image {
        anchors.fill: parent
        source: "qrc:/branding/scalable/apps/io.github.sandrewd.ReverieMediaPlayer.svg"
        // Rasterised at the displayed size rather than scaled from a default, so the stroke
        // stays crisp wherever the mark is used.
        sourceSize.width: Math.max(16, Math.round(root.width))
        sourceSize.height: Math.max(16, Math.round(root.height))
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
}
