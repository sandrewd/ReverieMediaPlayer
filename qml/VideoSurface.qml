import QtQuick
import Player

// The video stage. VideoItem is ours, fed RGBA frames from an appsink — see VideoItem.h for why
// qml6glsink is not used on this distro.
//
// Aspect ratio is the item's own business: it letterboxes internally, so forcing a shape here
// would letterbox twice.
Item {
    id: root

    VideoItem {
        id: surface
        anchors.fill: parent
        // Hand the surface over as soon as it exists rather than when something starts
        // playing, so the first frame of the first video has somewhere to go.
        Component.onCompleted: AudioEngine.setVideoItem(surface)
    }
}
