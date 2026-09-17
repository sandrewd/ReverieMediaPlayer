#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

// Video surface.
//
// This exists because `qml6glsink` is unusable on the target distro: Ubuntu's
// gstreamer1.0-qt6 loads and registers its module namespace but never registers the QML type,
// so `GstGLVideoItem` cannot be instantiated. Verified against element creation, a full
// gst_plugin_load_by_name, and the exported registration entry point called directly — all
// report success and none produce a type. See the brief.
//
// So frames arrive as ordinary RGBA buffers from an appsink and are drawn as a texture. That
// costs a copy and an upload per frame compared with a zero-copy GL path, which matters far
// less here than it sounds: on llvmpipe there is no GPU to keep fed, and the same cores do the
// compositing either way.
class VideoItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // The frame's own dimensions, so callers can reason about aspect without guessing.
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)

public:
    explicit VideoItem(QQuickItem *parent = nullptr);

    QSize frameSize() const;

public slots:
    // Called from the GStreamer streaming thread via a queued connection, so it lands here on
    // the GUI thread with the image already detached.
    void presentFrame(const QImage &frame);
    void clear();

signals:
    void frameSizeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;

private:
    mutable QMutex m_mutex;
    QImage m_frame;
    bool m_frameDirty = false;
};
