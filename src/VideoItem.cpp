#include "VideoItem.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

VideoItem::VideoItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QSize VideoItem::frameSize() const
{
    QMutexLocker lock(&m_mutex);
    return m_frame.size();
}

void VideoItem::presentFrame(const QImage &frame)
{
    {
        QMutexLocker lock(&m_mutex);
        const bool sizeChanged = frame.size() != m_frame.size();
        m_frame = frame;
        m_frameDirty = true;
        if (sizeChanged) {
            lock.unlock();
            emit frameSizeChanged();
        }
    }
    update();
}

void VideoItem::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_frame.isNull())
            return;
        m_frame = QImage();
        m_frameDirty = true;
    }
    emit frameSizeChanged();
    update();
}

QSGNode *VideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QImage frame;
    {
        QMutexLocker lock(&m_mutex);
        frame = m_frame;
        m_frameDirty = false;
    }

    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (frame.isNull() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }

    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }

    // A new texture per frame: QSGTexture has no in-place upload in this API, and reusing one
    // across frames would need the size to be constant anyway.
    if (QSGTexture *texture = window()->createTextureFromImage(frame)) {
        node->setTexture(texture);
    } else {
        qWarning("createTextureFromImage failed for a %dx%d frame", frame.width(), frame.height());
        delete node;
        return nullptr;
    }

    // Letterbox rather than stretch. The item fills whatever space the layout gives it, and
    // the video keeps its own shape inside that - anything else distorts faces.
    const qreal itemAspect = width() / height();
    const qreal frameAspect = qreal(frame.width()) / qreal(frame.height());
    QRectF target(0, 0, width(), height());
    if (frameAspect > itemAspect) {
        const qreal h = width() / frameAspect;
        target = QRectF(0, (height() - h) / 2.0, width(), h);
    } else if (frameAspect < itemAspect) {
        const qreal w = height() * frameAspect;
        target = QRectF((width() - w) / 2.0, 0, w, height());
    }
    node->setRect(target);
    return node;
}
