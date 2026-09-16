import QtQuick
import QtQuick.Window
import Player

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: "player — Phase 0 spike"
    color: "black"

    ProjectMItem {
        id: visualizer
        anchors.fill: parent
        renderScale: initialScale
        presetPath: initialPreset
    }

    // Frame-time overlay. Present from the first commit by design: this machine is reached
    // over RustDesk, which re-encodes the framebuffer at a capped rate, so observed
    // smoothness describes the stream and not the application.
    Rectangle {
        anchors { left: parent.left; top: parent.top; margins: 12 }
        width: stats.implicitWidth + 20
        height: stats.implicitHeight + 16
        color: "#c0000000"
        radius: 4

        Text {
            id: stats
            anchors.centerIn: parent
            font.family: "monospace"
            font.pixelSize: 13
            color: "#e8e8e8"
            text: "projectM   %1 ms\nthroughput %2 fps\nrender     %3x%4  (%5%)\nwindow     %6x%7"
                .arg(visualizer.frameTimeMs.toFixed(2))
                .arg(visualizer.fps.toFixed(1))
                .arg(visualizer.renderSize.width)
                .arg(visualizer.renderSize.height)
                .arg(Math.round(visualizer.renderScale * 100))
                .arg(root.width)
                .arg(root.height)
        }
    }

    Text {
        anchors { left: parent.left; bottom: parent.bottom; margins: 12 }
        font.family: "monospace"
        font.pixelSize: 11
        color: "#80e8e8e8"
        text: "[ / ] render scale    F fullscreen    Esc quit"
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_BracketLeft:
                visualizer.renderScale = Math.max(0.1, visualizer.renderScale - 0.1); break
            case Qt.Key_BracketRight:
                visualizer.renderScale = Math.min(1.0, visualizer.renderScale + 0.1); break
            case Qt.Key_F:
                root.visibility = root.visibility === Window.FullScreen
                    ? Window.Windowed : Window.FullScreen; break
            case Qt.Key_Escape:
                Qt.quit(); break
            }
        }
    }
}
