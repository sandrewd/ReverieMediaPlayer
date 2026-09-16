#pragma once

#include <QQuickFramebufferObject>
#include <QSize>
#include <QString>
#include <QtQml/qqmlregistration.h>

class AudioEngine;

// Phase 0 spike: projectM rendered into QML's scene graph via QQuickFramebufferObject.
// The Renderer half lives on the QQuick render thread with the GL context current, which
// is the thread affinity projectM requires for every one of its API calls.
class ProjectMItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT

    // Render scale is a real feature, not a debug knob: projectM renders to an offscreen
    // buffer at a fraction of item size and the scene graph upscales it. Section 4 of the
    // brief measured this as the only lever that moves frame time.
    Q_PROPERTY(qreal renderScale READ renderScale WRITE setRenderScale NOTIFY renderScaleChanged)
    Q_PROPERTY(QString presetPath READ presetPath WRITE setPresetPath NOTIFY presetPathChanged)
    Q_PROPERTY(AudioEngine *audioEngine READ audioEngine WRITE setAudioEngine NOTIFY audioEngineChanged)

    // Stats for the frame-time overlay. Observed smoothness over RustDesk tells you about
    // the video stream, not about us, so the app has to report its own numbers.
    Q_PROPERTY(qreal frameTimeMs READ frameTimeMs NOTIFY statsChanged)
    Q_PROPERTY(qreal fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(QSize renderSize READ renderSize NOTIFY statsChanged)

public:
    explicit ProjectMItem(QQuickItem *parent = nullptr);

    Renderer *createRenderer() const override;

    qreal renderScale() const { return m_renderScale; }
    void setRenderScale(qreal scale);

    QString presetPath() const { return m_presetPath; }
    void setPresetPath(const QString &path);

    AudioEngine *audioEngine() const { return m_audioEngine; }
    void setAudioEngine(AudioEngine *engine);

    qreal frameTimeMs() const { return m_frameTimeMs; }
    qreal fps() const { return m_fps; }
    QSize renderSize() const { return m_renderSize; }

public slots:
    // Called from the render thread via a queued connection, so it lands on the GUI thread.
    void applyStats(qreal frameTimeMs, qreal fps, int width, int height);

signals:
    void renderScaleChanged();
    void presetPathChanged();
    void audioEngineChanged();
    void statsChanged();

private:
    qreal m_renderScale = 0.5;
    QString m_presetPath;
    AudioEngine *m_audioEngine = nullptr;
    qreal m_frameTimeMs = 0.0;
    qreal m_fps = 0.0;
    QSize m_renderSize;
};
