#include "ProjectMItem.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QQuickWindow>
#include <QtMath>
#include <QDebug>

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>

#include <QRandomGenerator>
#include <cmath>

#include "AudioEngine.h"


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
        if (m_playlist)
            projectm_playlist_destroy(m_playlist);
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

            // The playlist library owns preset ordering, shuffle and the automatic advance
            // when a preset's time is up. Connecting it means projectM asks it for the next
            // preset itself, so we do not have to drive a timer.
            m_playlist = projectm_playlist_create(m_pm);
            projectm_playlist_set_shuffle(m_playlist, true);
            projectm_playlist_connect(m_playlist, m_pm);

            m_pendingPreset = true;
        }

        qInfo("createFramebufferObject: requested %dx%d -> fbo %dx%d (scale %.2f)",
              size.width(), size.height(), m_fboSize.width(), m_fboSize.height(), m_renderScale);
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
            qInfo("invalidating fbo: scale %.2f -> %.2f, item %dx%d, current fbo %dx%d",
                  m_renderScale, wantedScale, itemSize.width(), itemSize.height(),
                  m_fboSize.width(), m_fboSize.height());
            m_renderScale = wantedScale;
            invalidateFramebufferObject();
        }

        m_ring = pmItem->audioEngine() ? pmItem->audioEngine()->ringBuffer() : nullptr;

        const QString wanted = pmItem->presetPath();
        if (wanted != m_presetPath) {
            m_presetPath = wanted;
            m_pendingPreset = true;
        }

        const QString wantedLibrary = pmItem->presetsPath();
        const QString wantedCurated = pmItem->curatedList();
        if (wantedLibrary != m_presetsPath || wantedCurated != m_curatedList) {
            m_presetsPath = wantedLibrary;
            m_curatedList = wantedCurated;
            m_pendingLibrary = true;
        }

        m_adaptive = pmItem->adaptiveQuality();
        m_targetFps = pmItem->targetFps();
        m_wantLocked = pmItem->presetLocked();
        m_wantShuffle = pmItem->shuffle();
        m_wantDuration = pmItem->presetDuration();
        m_commands.append(pmItem->takeCommands());

    }

    void render() override
    {
        if (!m_pm) {
            update();
            return;
        }

        if (m_pendingLibrary) {
            m_pendingLibrary = false;
            loadLibrary();
        }

        if (m_pendingPreset) {
            m_pendingPreset = false;
            if (!m_presetPath.isEmpty()) {
                projectm_load_preset_file(m_pm, m_presetPath.toUtf8().constData(), false);
                qInfo().noquote() << "loaded preset:" << m_presetPath;
            }
        }

        applySettings();
        runCommands();

        feedAudio();

        QElapsedTimer timer;
        timer.start();

        // Qt 6 has no resetOpenGLState(); bracketing the foreign GL calls is how you tell
        // the scene graph that its cached state assumptions no longer hold.
        QQuickWindow *win = m_item ? m_item->window() : nullptr;
        if (win)
            win->beginExternalCommands();

        // projectM <= 4.1.7 hardcodes glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) for its final
        // composite, so its output lands on the window's default framebuffer and the scene
        // graph then clears over it - the item renders black while still costing full frame
        // time. 4.2.0 adds an explicit target, which is the only correct fix.
#if defined(PROJECTM_HAS_RENDER_FRAME_FBO)
        if (QOpenGLFramebufferObject *target = framebufferObject())
            projectm_opengl_render_frame_fbo(m_pm, target->handle());
        else
            projectm_opengl_render_frame(m_pm);
#else
        projectm_opengl_render_frame(m_pm);
#endif

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

        // Push stats straight to the GUI thread a few times a second. A queued invocation is
        // safe across threads; doing it every frame would just flood the event loop.
        publishPreset();
        adaptQuality();

        if (!m_statsPosted.isValid() || m_statsPosted.elapsed() >= 200) {
            m_statsPosted.restart();
            if (m_item) {
                QMetaObject::invokeMethod(m_item, "applyStats", Qt::QueuedConnection,
                                          Q_ARG(qreal, m_lastFrameMs),
                                          Q_ARG(qreal, m_fps),
                                          Q_ARG(int, m_fboSize.width()),
                                          Q_ARG(int, m_fboSize.height()));
            }
        }

        // Dump the offscreen buffer itself, before the scene graph upscales it. Comparing
        // this against --capture separates projectM's output from our compositing.
        static const QString dumpPath = qEnvironmentVariable("PLAYER_DUMP_FBO");
        if (!dumpPath.isEmpty() && m_frameCount == 90) {
            if (QOpenGLFramebufferObject *fbo = framebufferObject()) {
                if (fbo->toImage().save(dumpPath))
                    qInfo("dumped fbo to %s", qPrintable(dumpPath));
            }
        }

        static const bool probe = qEnvironmentVariableIsSet("PLAYER_PROBE");
        if (probe && m_frameCount % 30 == 0) {
            if (QOpenGLFramebufferObject *fbo = framebufferObject()) {
                const QImage img = fbo->toImage();
                quint64 sum = 0;
                int nonBlack = 0;
                const int step = qMax(1, img.width() / 64);
                int samples = 0;
                for (int y = 0; y < img.height(); y += step) {
                    for (int x = 0; x < img.width(); x += step) {
                        const QRgb px = img.pixel(x, y);
                        const int lum = qGray(px);
                        sum += lum;
                        if (lum > 8)
                            ++nonBlack;
                        ++samples;
                    }
                }
                qInfo("  probe: fbo %dx%d mean-luma %.1f non-black %d/%d (%.0f%%)",
                      img.width(), img.height(), samples ? double(sum) / samples : 0.0,
                      nonBlack, samples, samples ? 100.0 * nonBlack / samples : 0.0);
            } else {
                qWarning("  probe: framebufferObject() is null");
            }
        }

        if (++m_frameCount % 30 == 0) {
            qInfo("frame %5d | projectM %6.2f ms | %5.1f fps | fbo %dx%d",
                  m_frameCount, m_lastFrameMs, m_fps, m_fboSize.width(), m_fboSize.height());
        }

        if (win)
            win->endExternalCommands();

        update();
    }

private:
    void loadLibrary()
    {
        if (!m_playlist || m_presetsPath.isEmpty())
            return;

        projectm_playlist_clear(m_playlist);

        uint32_t added = 0;
        // A curated list keeps the default experience to a few hundred presets. The full
        // corpus stays one menu item away rather than being the thing a new user lands in.
        if (!m_curatedList.isEmpty()) {
            QFile listFile(m_curatedList);
            if (listFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QDir base(m_presetsPath);
                QTextStream in(&listFile);
                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                        continue;
                    const QString full = base.absoluteFilePath(line);
                    if (projectm_playlist_add_preset(m_playlist, full.toUtf8().constData(), false))
                        ++added;
                }
                qInfo("preset library: %u curated presets", added);
            } else {
                qWarning("preset library: cannot read curated list %s", qPrintable(m_curatedList));
            }
        }

        if (added == 0) {
            added = projectm_playlist_add_path(
                m_playlist, m_presetsPath.toUtf8().constData(), true, false);
            qInfo("preset library: %u presets from %s", added, qPrintable(m_presetsPath));
        }

        if (added > 0) {
            // Start somewhere other than the first preset every launch; the corpus is
            // alphabetical and always opening on the same one feels broken.
            projectm_playlist_set_position(m_playlist, QRandomGenerator::global()->bounded(int(added)), true);
        }
        m_presetDirty = true;
    }

    void applySettings()
    {
        if (projectm_get_preset_locked(m_pm) != m_wantLocked)
            projectm_set_preset_locked(m_pm, m_wantLocked);
        if (m_playlist && projectm_playlist_get_shuffle(m_playlist) != m_wantShuffle)
            projectm_playlist_set_shuffle(m_playlist, m_wantShuffle);
        if (!qFuzzyCompare(m_appliedDuration, m_wantDuration)) {
            m_appliedDuration = m_wantDuration;
            projectm_set_preset_duration(m_pm, m_wantDuration);
        }
    }

    void runCommands()
    {
        if (!m_playlist || m_commands.isEmpty())
            return;
        const uint32_t size = projectm_playlist_size(m_playlist);
        for (int command : std::as_const(m_commands)) {
            switch (command) {
            case ProjectMItem::CommandNext:
                projectm_playlist_play_next(m_playlist, false);
                break;
            case ProjectMItem::CommandPrevious:
                projectm_playlist_play_previous(m_playlist, false);
                break;
            case ProjectMItem::CommandRandom:
                if (size > 0) {
                    projectm_playlist_set_position(
                        m_playlist, QRandomGenerator::global()->bounded(int(size)), false);
                }
                break;
            case ProjectMItem::CommandReload:
                m_pendingLibrary = true;
                break;
            }
        }
        m_commands.clear();
        m_presetDirty = true;
    }

    // Steps the render scale to hold the target frame rate. Deliberately slow and
    // hysteretic: a visualiser whose resolution visibly pumps up and down is worse than one
    // that is simply a bit soft. Down-steps react faster than up-steps, because dropping
    // frames is more objectionable than running below the achievable quality.
    void adaptQuality()
    {
        if (!m_adaptive || !m_item || m_fps <= 0.0)
            return;

        // A preset switch recompiles shaders and spikes frame time; ignore that window.
        if (m_presetSettleTimer.isValid() && m_presetSettleTimer.elapsed() < 1500)
            return;

        const qreal current = m_renderScale;
        qreal proposed = current;

        if (m_fps < m_targetFps * 0.85) {
            if (!m_downTimer.isValid())
                m_downTimer.start();
            m_upTimer.invalidate();
            if (m_downTimer.elapsed() > 1500) {
                proposed = qMax(0.2, current - 0.1);
                m_downTimer.restart();
            }
        } else if (m_fps > m_targetFps * 1.35) {
            if (!m_upTimer.isValid())
                m_upTimer.start();
            m_downTimer.invalidate();
            if (m_upTimer.elapsed() > 5000) {
                proposed = qMin(1.0, current + 0.1);
                m_upTimer.restart();
            }
        } else {
            m_downTimer.invalidate();
            m_upTimer.invalidate();
        }

        if (!qFuzzyCompare(proposed, current)) {
            QMetaObject::invokeMethod(m_item, "applyAdaptiveScale", Qt::QueuedConnection,
                                      Q_ARG(qreal, proposed));
        }
    }

    // Polled rather than driven by a callback so that projectM's own automatic advance,
    // which happens without us asking, is reported too.
    void publishPreset()
    {
        if (!m_playlist || !m_item)
            return;
        const uint32_t index = projectm_playlist_get_position(m_playlist);
        const uint32_t count = projectm_playlist_size(m_playlist);
        if (!m_presetDirty && index == m_lastIndex && count == m_lastCount)
            return;

        m_presetDirty = false;
        m_lastIndex = index;
        m_lastCount = count;
        m_presetSettleTimer.restart();

        QString name;
        if (count > 0) {
            if (char *item = projectm_playlist_item(m_playlist, index)) {
                name = QFileInfo(QString::fromUtf8(item)).completeBaseName();
                projectm_playlist_free_string(item);
            }
        }
        QMetaObject::invokeMethod(m_item, "applyPreset", Qt::QueuedConnection,
                                  Q_ARG(QString, name), Q_ARG(int, int(index)),
                                  Q_ARG(int, int(count)));
    }

    QSize scaledSize(const QSize &itemSize) const
    {
        return QSize(qMax(16, qRound(itemSize.width() * m_renderScale)),
                     qMax(16, qRound(itemSize.height() * m_renderScale)));
    }

    void feedAudio()
    {
        float samples[kSamplesPerFrame * 2];

        // Real audio when the pipeline is delivering it; the synthetic sweep otherwise, so
        // the visualiser is never a still image while the user is browsing a playlist.
        if (m_ring && m_ring->readLatest(samples, kSamplesPerFrame)) {
            m_audioIsLive = true;
            if (qEnvironmentVariableIsSet("PLAYER_PROBE") && m_frameCount % 60 == 0) {
                double sum = 0.0, peak = 0.0;
                for (int i = 0; i < kSamplesPerFrame * 2; ++i) {
                    sum += double(samples[i]) * samples[i];
                    peak = qMax(peak, qAbs(double(samples[i])));
                }
                qInfo("  audio: LIVE  rms %.4f  peak %.4f", std::sqrt(sum / (kSamplesPerFrame * 2)), peak);
            }
            projectm_pcm_add_float(m_pm, samples, kSamplesPerFrame, PROJECTM_STEREO);
            return;
        }
        m_audioIsLive = false;
        if (qEnvironmentVariableIsSet("PLAYER_PROBE") && m_frameCount % 60 == 0)
            qInfo("  audio: synthetic fallback (no PCM arriving)");

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
    projectm_playlist_handle m_playlist = nullptr;
    ProjectMItem *m_item = nullptr;
    AudioRingBuffer *m_ring = nullptr;
    bool m_audioIsLive = false;
    QString m_presetsPath;
    QString m_curatedList;
    QList<int> m_commands;
    bool m_pendingLibrary = false;
    bool m_presetDirty = false;
    bool m_wantLocked = false;
    bool m_wantShuffle = true;
    qreal m_wantDuration = 30.0;
    qreal m_appliedDuration = 30.0;
    uint32_t m_lastIndex = 0;
    uint32_t m_lastCount = 0;
    bool m_adaptive = true;
    qreal m_targetFps = 30.0;
    QElapsedTimer m_downTimer;
    QElapsedTimer m_upTimer;
    QElapsedTimer m_presetSettleTimer;
    qreal m_renderScale = 0.5;
    QSize m_fboSize;
    QString m_presetPath;
    bool m_pendingPreset = false;
    QElapsedTimer m_statsPosted;
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

void ProjectMItem::setAudioEngine(AudioEngine *engine)
{
    if (engine == m_audioEngine)
        return;
    m_audioEngine = engine;
    emit audioEngineChanged();
    update();
}

void ProjectMItem::setPresetsPath(const QString &path)
{
    if (path == m_presetsPath)
        return;
    m_presetsPath = path;
    emit presetsPathChanged();
    update();
}

void ProjectMItem::setCuratedList(const QString &path)
{
    if (path == m_curatedList)
        return;
    m_curatedList = path;
    emit curatedListChanged();
    update();
}

void ProjectMItem::setPresetLocked(bool locked)
{
    if (locked == m_presetLocked)
        return;
    m_presetLocked = locked;
    emit presetLockedChanged();
    update();
}

void ProjectMItem::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    emit shuffleChanged();
    update();
}

void ProjectMItem::setPresetDuration(qreal seconds)
{
    seconds = qBound(5.0, seconds, 600.0);
    if (qFuzzyCompare(seconds, m_presetDuration))
        return;
    m_presetDuration = seconds;
    emit presetDurationChanged();
    update();
}

void ProjectMItem::nextPreset()
{
    QMutexLocker lock(&m_commandMutex);
    m_commands.append(CommandNext);
    lock.unlock();
    update();
}

void ProjectMItem::previousPreset()
{
    QMutexLocker lock(&m_commandMutex);
    m_commands.append(CommandPrevious);
    lock.unlock();
    update();
}

void ProjectMItem::randomPreset()
{
    QMutexLocker lock(&m_commandMutex);
    m_commands.append(CommandRandom);
    lock.unlock();
    update();
}

QList<int> ProjectMItem::takeCommands()
{
    QMutexLocker lock(&m_commandMutex);
    return std::move(m_commands);
}

void ProjectMItem::setAdaptiveQuality(bool enabled)
{
    if (enabled == m_adaptiveQuality)
        return;
    m_adaptiveQuality = enabled;
    emit adaptiveQualityChanged();
    update();
}

void ProjectMItem::setTargetFps(qreal fps)
{
    fps = qBound(15.0, fps, 60.0);
    if (qFuzzyCompare(fps, m_targetFps))
        return;
    m_targetFps = fps;
    emit targetFpsChanged();
    update();
}

void ProjectMItem::applyAdaptiveScale(qreal scale)
{
    // Ignore a suggestion that raced with the user turning the feature off.
    if (!m_adaptiveQuality)
        return;
    setRenderScale(scale);
}

void ProjectMItem::applyPreset(const QString &name, int index, int count)
{
    m_presetName = name;
    m_presetIndex = index;
    m_presetCount = count;
    emit presetChanged();
}

void ProjectMItem::applyStats(qreal frameTimeMs, qreal fps, int width, int height)
{
    m_frameTimeMs = frameTimeMs;
    m_fps = fps;
    m_renderSize = QSize(width, height);
    emit statsChanged();
}

