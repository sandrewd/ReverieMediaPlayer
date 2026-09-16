#include "ProjectMItem.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QtMath>
#include <QDebug>

#include <projectM-4/projectM.h>

namespace {

// Phase 0 has no audio pipeline yet, so the visualizer is driven by a synthetic tone:
// a slow sweep with a couple of harmonics, enough to make presets react visibly.
constexpr int kSamplesPerFrame = 512;
constexpr double kSampleRate = 44100.0;

class ProjectMRenderer : public QQuickFramebufferObject::Renderer
{
public:
    ~ProjectMRenderer() override
    {
        if (m_pm)
            projectm_destroy(m_pm);
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        // Qt hands us the item size; we render smaller and let the scene graph upscale.
        m_fboSize = scaledSize(size);

        if (!m_pm) {
            m_pm = projectm_create();
            if (!m_pm) {
                qWarning() << "projectm_create() failed - no GL context, or context too old";
                return new QOpenGLFramebufferObject(size);
            }
            // Mesh size is deliberately left at the default. Section 4 measured mesh size as
            // irrelevant (48x32 and 16x12 both land at ~87ms); the workload is fragment-bound.
            projectm_set_fps(m_pm, 60);
            projectm_set_preset_duration(m_pm, 30.0);
            m_pendingPreset = true;
        }

        projectm_set_window_size(m_pm, m_fboSize.width(), m_fboSize.height());

        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(0);
        return new QOpenGLFramebufferObject(m_fboSize, format);
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        // Runs with the GUI thread blocked: the only safe place to exchange state.
        auto *pmItem = static_cast<ProjectMItem *>(item);
        m_item = pmItem;

        // A scale or size change means the offscreen buffer is the wrong size. There is no
        // API to resize it, so drop it and let Qt ask for a new one on the next frame.
        const qreal wantedScale = pmItem->renderScale();
        const QSize itemSize(qRound(pmItem->width()), qRound(pmItem->height()));
        if (!qFuzzyCompare(wantedScale, m_renderScale) || scaledSize(itemSize) != m_fboSize) {
            m_renderScale = wantedScale;
            invalidateFramebufferObject();
        }

        const QString wanted = pmItem->presetPath();
        if (wanted != m_presetPath) {
            m_presetPath = wanted;
            m_pendingPreset = true;
        }

        if (m_statsDirty) {
            QMetaObject::invokeMethod(pmItem, "applyStats", Qt::QueuedConnection,
                                      Q_ARG(qreal, m_lastFrameMs),
                                      Q_ARG(qreal, m_fps),
                                      Q_ARG(int, m_fboSize.width()),
                                      Q_ARG(int, m_fboSize.height()));
            m_statsDirty = false;
        }
    }

    void render() override
    {
        if (!m_pm) {
            update();
            return;
        }

        if (m_pendingPreset) {
            m_pendingPreset = false;
            if (!m_presetPath.isEmpty()) {
                projectm_load_preset_file(m_pm, m_presetPath.toUtf8().constData(), false);
                qInfo().noquote() << "loaded preset:" << m_presetPath;
            }
        }

        feedSyntheticAudio();

        QElapsedTimer timer;
        timer.start();

        // Qt 6 has no resetOpenGLState(); bracketing the foreign GL calls is how you tell
        // the scene graph that its cached state assumptions no longer hold.
        QQuickWindow *win = m_item ? m_item->window() : nullptr;
        if (win)
            win->beginExternalCommands();

        projectm_opengl_render_frame(m_pm);

        // llvmpipe defers work aggressively, so without a flush the measured time is the
        // time to queue commands, not the time to draw. But the flush also costs throughput:
        // it stops llvmpipe overlapping rasterisation with the next frame's setup. So the
        // honest per-frame cost and the best achievable frame rate cannot both be measured
        // in the same run. PLAYER_NO_GLFINISH=1 measures throughput instead.
        static const bool skipFinish = qEnvironmentVariableIsSet("PLAYER_NO_GLFINISH");
        if (!skipFinish) {
            if (auto *ctx = QOpenGLContext::currentContext())
                ctx->functions()->glFinish();
        }

        const double renderMs = timer.nsecsElapsed() / 1.0e6;

        // Exponential moving average: a raw per-frame number is unreadable on screen.
        m_lastFrameMs = m_lastFrameMs > 0.0 ? (m_lastFrameMs * 0.9 + renderMs * 0.1) : renderMs;

        // Throughput measured between successive frames, which includes everything else
        // the render thread does, not just projectM.
        if (m_intervalTimer.isValid()) {
            const double intervalMs = m_intervalTimer.nsecsElapsed() / 1.0e6;
            if (intervalMs > 0.0) {
                const double instantFps = 1000.0 / intervalMs;
                m_fps = m_fps > 0.0 ? (m_fps * 0.9 + instantFps * 0.1) : instantFps;
            }
        }
        m_intervalTimer.restart();

        m_statsDirty = true;

        if (++m_frameCount % 30 == 0) {
            qInfo("frame %5d | projectM %6.2f ms | %5.1f fps | fbo %dx%d",
                  m_frameCount, m_lastFrameMs, m_fps, m_fboSize.width(), m_fboSize.height());
        }

        if (win)
            win->endExternalCommands();

        update();
    }

private:
    QSize scaledSize(const QSize &itemSize) const
    {
        return QSize(qMax(16, qRound(itemSize.width() * m_renderScale)),
                     qMax(16, qRound(itemSize.height() * m_renderScale)));
    }

    void feedSyntheticAudio()
    {
        float samples[kSamplesPerFrame * 2];
        const double sweep = 220.0 + 160.0 * std::sin(m_sweepPhase);
        m_sweepPhase += 0.01;

        for (int i = 0; i < kSamplesPerFrame; ++i) {
            const double t = m_phase + i / kSampleRate;
            const double v = 0.55 * std::sin(2.0 * M_PI * sweep * t)
                           + 0.25 * std::sin(2.0 * M_PI * sweep * 2.0 * t)
                           + 0.20 * std::sin(2.0 * M_PI * 55.0 * t);
            samples[i * 2] = static_cast<float>(v);
            samples[i * 2 + 1] = static_cast<float>(v * 0.85);
        }
        m_phase += kSamplesPerFrame / kSampleRate;

        projectm_pcm_add_float(m_pm, samples, kSamplesPerFrame, PROJECTM_STEREO);
    }

    projectm_handle m_pm = nullptr;
    ProjectMItem *m_item = nullptr;
    qreal m_renderScale = 0.5;
    QSize m_fboSize;
    QString m_presetPath;
    bool m_pendingPreset = false;
    bool m_statsDirty = false;
    double m_lastFrameMs = 0.0;
    double m_fps = 0.0;
    double m_phase = 0.0;
    double m_sweepPhase = 0.0;
    int m_frameCount = 0;
    QElapsedTimer m_intervalTimer;
};

} // namespace

ProjectMItem::ProjectMItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    // Render scale requires taking manual control of the texture size; with
    // textureFollowsItemSize the FBO would always match the item.
    setTextureFollowsItemSize(false);
    // projectM draws with an OpenGL bottom-left origin; Qt expects top-left.
    setMirrorVertically(true);
}

QQuickFramebufferObject::Renderer *ProjectMItem::createRenderer() const
{
    return new ProjectMRenderer;
}

void ProjectMItem::setRenderScale(qreal scale)
{
    scale = qBound(0.1, scale, 1.0);
    if (qFuzzyCompare(scale, m_renderScale))
        return;
    m_renderScale = scale;
    emit renderScaleChanged();
    update();
}

void ProjectMItem::setPresetPath(const QString &path)
{
    if (path == m_presetPath)
        return;
    m_presetPath = path;
    emit presetPathChanged();
    update();
}

void ProjectMItem::applyStats(qreal frameTimeMs, qreal fps, int width, int height)
{
    m_frameTimeMs = frameTimeMs;
    m_fps = fps;
    m_renderSize = QSize(width, height);
    emit statsChanged();
}

