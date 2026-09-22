#include "ProjectMItem.h"
#include "Logging.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSet>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QQuickWindow>
#include <QtMath>
#include <QDebug>
#include <atomic>

#include <projectM-4/projectM.h>

// projectM reports a preset it cannot compile only through its log callback, and then quietly
// keeps the previous one. Set and read on the render thread, immediately either side of the
// command that could have caused it.
static std::atomic_bool g_presetCompileFailed{false};
#include <projectM-4/logging.h>
#include <projectM-4/callbacks.h>
#include <projectM-4/playlist.h>

#include <QRandomGenerator>
#include <QSettings>
#include <cmath>

#ifdef Q_OS_LINUX
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "AudioEngine.h"


namespace {

// Phase 0 has no audio pipeline yet, so the visualizer is driven by a synthetic tone:
// a slow sweep with a couple of harmonics, enough to make presets react visibly.
// The most audio handed to projectM in one frame. It is a ceiling, not a quota: one frame at
// 60fps is ~735 samples at 44.1kHz and ~1225 at 36fps, so 512 - the old fixed read size - would
// have left the consumer permanently behind and spliced the audio right back together. 4096
// covers everything down to about 11fps.
constexpr int kMaxFeedFrames = 4096;
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
            // What context did we actually get? We ask for 3.3 core, but a request is not a
            // grant, and every measurement in this project was taken on llvmpipe or AMD. A
            // driver that silently hands back something else is invisible without this.
            if (QOpenGLContext *ctx = QOpenGLContext::currentContext()) {
                auto *gl = ctx->functions();
                const QSurfaceFormat fmt = ctx->format();
                qInfo("gl: %s | %s | %s | context %d.%d %s",
                      gl->glGetString(GL_VENDOR) ? reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)) : "?",
                      gl->glGetString(GL_RENDERER) ? reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)) : "?",
                      gl->glGetString(GL_VERSION) ? reinterpret_cast<const char *>(gl->glGetString(GL_VERSION)) : "?",
                      fmt.majorVersion(), fmt.minorVersion(),
                      fmt.profile() == QSurfaceFormat::CoreProfile ? "core"
                          : fmt.profile() == QSurfaceFormat::CompatibilityProfile ? "compat" : "none");
            }

            // projectM knows why it failed; we were throwing that away. Shader compilation is
            // where drivers differ most - Mesa accepts GLSL that NVIDIA rejects - and a preset
            // that will not compile renders as nothing at all.
            // Default is INFO, so warnings and errors already arrive - but say so explicitly,
            // because a silent projectM is otherwise indistinguishable from a healthy one.
            projectm_set_log_level(PROJECTM_LOG_LEVEL_DEBUG, false);
            projectm_set_log_callback(
                [](const char *message, projectm_log_level level, void *) {
                    if (!message)
                        return;
                    // A missing external texture is reported once per preset that wants one,
                    // which is 23% of the corpus, and there is nothing the reader can do about
                    // it beyond installing a texture pack. That makes it diagnostic detail
                    // rather than a warning - the same call §9n made about the frame timer.
                    const bool missingTexture = strstr(message, "Failed to find requested texture");
                    if (strstr(message, "Could not compile") || strstr(message, "Could not load")
                        || strstr(message, "Could not parse"))
                        g_presetCompileFailed.store(true);
                    if (level >= PROJECTM_LOG_LEVEL_WARN && !missingTexture)
                        qWarning("projectM: %s", message);
                    else
                        qCDebug(lcRender, "projectM: %s", message);
                },
                false, nullptr);

            m_pm = projectm_create();
            if (!m_pm) {
                qWarning() << "projectm_create() failed - no GL context, or context too old";
                return new QOpenGLFramebufferObject(size);
            }

            // A preset that fails to load is the difference between "the visualiser is broken"
            // and "this preset is broken", and only projectM can tell them apart.
            projectm_set_preset_switch_failed_event_callback(
                m_pm,
                [](const char *preset, const char *message, void *) {
                    qWarning("projectM: preset failed: %s (%s)",
                             preset ? preset : "?", message ? message : "no detail");
                },
                nullptr);
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
            m_pendingTextures = true;
        }

        qCDebug(lcRender, "createFramebufferObject: requested %dx%d -> fbo %dx%d (scale %.2f)",
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
#ifdef Q_OS_LINUX
        // This is the render thread, and the GUI thread is blocked, so it is the one moment
        // where handing the tid over needs no care.
        pmItem->publishRenderThread(static_cast<int>(syscall(SYS_gettid)));
#endif

        // A scale or size change means the offscreen buffer is the wrong size. There is no
        // API to resize it, so drop it and let Qt ask for a new one on the next frame.
        const qreal wantedScale = pmItem->renderScale();
        // In DEVICE pixels, which is what createFramebufferObject is handed. The item's own
        // width() and height() are logical, so on a high-DPI screen the two never agree: the
        // comparison below fired on every frame, the buffer was destroyed and rebuilt ~44 times
        // a second, and Milkdrop presets - which draw on top of the previous frame - had nothing
        // to accumulate into. The visualiser was simply black, with no error anywhere.
        //
        // Measured on a 4K screen: item 753x592 logical against an FBO built for 1506x1184.
        // On a 1x display the two are equal, which is why this survived every test until one
        // ran on a high-DPI machine.
        const qreal dpr = pmItem->window() ? pmItem->window()->effectiveDevicePixelRatio() : 1.0;
        const QSizeF settled = pmItem->settledSize();
        const QSize itemSize(qRound(settled.width() * dpr), qRound(settled.height() * dpr));
        if (!qFuzzyCompare(wantedScale, m_renderScale) || scaledSize(itemSize) != m_fboSize) {
            qCDebug(lcRender, "invalidating fbo: scale %.2f -> %.2f, item %dx%d, current fbo %dx%d",
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
        const QStringList wantedBlocklist = pmItem->blocklists();
        const bool wantedShowBroken = pmItem->showBroken();
        if (wantedLibrary != m_presetsPath || wantedCurated != m_curatedList
            || wantedBlocklist != m_blocklists || wantedShowBroken != m_showBroken) {
            m_presetsPath = wantedLibrary;
            m_curatedList = wantedCurated;
            m_blocklists = wantedBlocklist;
            m_showBroken = wantedShowBroken;
            m_pendingLibrary = true;
        }

        const QString wantedTextures = pmItem->texturesPath();
        if (wantedTextures != m_texturesPath) {
            m_texturesPath = wantedTextures;
            m_pendingTextures = true;
        }

        m_active = pmItem->active();
        m_selfDriven = pmItem->maxFps() <= 0;
        m_yieldToDesktop = pmItem->yieldToDesktop();
        const QStringList wantedList = pmItem->presetList();
        if (wantedList != m_explicitPaths) {
            m_explicitPaths = wantedList;
            m_pendingLibrary = true;
        }
        const int jump = pmItem->takePendingJump();
        if (jump >= 0)
            m_pendingJumpIndex = jump;
        m_adaptive = pmItem->adaptiveQuality();
        m_targetFps = pmItem->targetFps();
        m_wantLocked = pmItem->presetLocked();
        m_wantShuffle = pmItem->shuffle();
        m_wantDuration = pmItem->presetDuration();
        m_commands.append(pmItem->takeCommands());

    }

    void render() override
    {
        // The visualiser is the least important thing on the machine. On a software renderer
        // it will happily use a core and a half, and if it competes on equal terms with the
        // window manager and the compositor the rest of the desktop stops repainting - panel
        // items go undrawn until hovered, regions stay stale. Nice only this thread: audio
        // runs on GStreamer's own threads and must keep its priority.
#ifdef Q_OS_LINUX
        if (m_item)
            m_item->syncScheduling();
#endif

        if (!m_pm) {
            update();
            return;
        }

        if (m_pendingTextures) {
            m_pendingTextures = false;
            applyTextureSearchPaths();
        }

        if (m_pendingLibrary) {
            m_pendingLibrary = false;
            loadLibrary();
        }

        if (m_pendingPreset) {
            m_pendingPreset = false;
            if (!m_presetPath.isEmpty()) {
                projectm_load_preset_file(m_pm, m_presetPath.toUtf8().constData(), false);
                qCDebug(lcPreset).noquote() << "loaded preset:" << m_presetPath;
            }
        }

        applySettings();
        runCommands();
        if (g_presetCompileFailed.exchange(false) && m_item && !m_requestedPreset.isEmpty()) {
            const QString failed = QFileInfo(m_requestedPreset).completeBaseName();
            m_requestedPreset.clear();
            QMetaObject::invokeMethod(m_item, "reportPresetFailed", Qt::QueuedConnection,
                                      Q_ARG(QString, failed));
        }
        publishPreset();

        if (!m_active) {
            // Clear once, then stop asking for frames. Not calling update() lets the render
            // thread go quiet, which matters on a software renderer: an idle player should
            // not be burning two cores drawing something nobody asked for.
            if (!m_idleCleared) {
                if (auto *ctx = QOpenGLContext::currentContext()) {
                    ctx->functions()->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    ctx->functions()->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
                m_idleCleared = true;
                m_fps = 0.0;
                m_lastFrameMs = 0.0;
                if (m_item) {
                    QMetaObject::invokeMethod(m_item, "applyStats", Qt::QueuedConnection,
                                              Q_ARG(qreal, 0.0), Q_ARG(qreal, 0.0),
                                              Q_ARG(int, m_fboSize.width()),
                                              Q_ARG(int, m_fboSize.height()));
                }
            }
            return;
        }
        m_idleCleared = false;

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
        // A core profile requires a vertex array object to be bound for any draw call. Mesa
        // quietly accepts the default VAO 0; NVIDIA enforces the rule and turns those draws
        // into no-ops, which is exactly what a visualiser that initialises, consumes audio and
        // produces an entirely black frame looks like. Binding one costs nothing where it is
        // not needed.
        auto *ctx = QOpenGLContext::currentContext();
        if (ctx && !m_vao.isCreated())
            m_vao.create();
        if (m_vao.isCreated())
            m_vao.bind();

        // Nothing in this codebase has ever checked a GL error. A driver that rejects a call
        // says so here and nowhere else - projectM reported no problem at all while drawing
        // nothing. Drained first so what follows is attributable to projectM.
        auto *fns = ctx ? ctx->functions() : nullptr;
        if (fns)
            while (fns->glGetError() != GL_NO_ERROR) { }

#if defined(PROJECTM_HAS_RENDER_FRAME_FBO)
        if (QOpenGLFramebufferObject *target = framebufferObject()) {
            if (fns && !m_fboChecked) {
                m_fboChecked = true;
                const GLenum status = fns->glCheckFramebufferStatus(GL_FRAMEBUFFER);
                qInfo("gl: render target fbo %u, %dx%d, status 0x%04x%s",
                      target->handle(), target->width(), target->height(), status,
                      status == GL_FRAMEBUFFER_COMPLETE ? " (complete)" : " (INCOMPLETE)");
            }
            projectm_opengl_render_frame_fbo(m_pm, target->handle());
        } else {
            projectm_opengl_render_frame(m_pm);
        }
#else
        projectm_opengl_render_frame(m_pm);
#endif

        if (fns) {
            GLenum err = fns->glGetError();
            while (err != GL_NO_ERROR) {
                // Once per distinct code: a per-frame flood would be unreadable and would
                // itself slow the render thread.
                if (!m_seenGlErrors.contains(err)) {
                    m_seenGlErrors.insert(err);
                    qWarning("gl: projectM render raised error 0x%04x", err);
                }
                err = fns->glGetError();
            }
        }

        if (m_vao.isCreated())
            m_vao.release();

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

        // Tell projectM the rate we are actually achieving, not a constant.
        //
        // Milkdrop presets normalise their own animation by the `fps` variable - "t = t + .001/fps"
        // and "pow(0.96, 30/fps)" are the standard idioms, and 69 of the 304 presets in one
        // category alone use them. projectM passes this value straight through to the preset, so
        // reporting a fixed 60 while rendering at something else makes every such preset run at
        // real_fps/60 of the speed its author tuned: three times too slow at 20fps, and too fast
        // on anything that beats the cap.
        //
        // A threshold rather than every frame, because the value is already smoothed and a
        // preset integrating 1/fps does not want it jittering underneath it.
        if (m_pm && m_fps > 0.0) {
            const int rounded = qBound(5, int(std::lround(m_fps)), 240);
            if (std::abs(rounded - m_reportedFps) >= 2) {
                m_reportedFps = rounded;
                projectm_set_fps(m_pm, m_reportedFps);
            }
        }

        // Push stats straight to the GUI thread a few times a second. A queued invocation is
        // safe across threads; doing it every frame would just flood the event loop.
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
            qCDebug(lcRender, "frame %5d | projectM %6.2f ms | %5.1f fps | fbo %dx%d",
                  m_frameCount, m_lastFrameMs, m_fps, m_fboSize.width(), m_fboSize.height());
        }

        if (win)
            win->endExternalCommands();

        // With a frame cap the item's timer asks for the next frame; asking for it here too
        // would defeat the cap.
        if (m_selfDriven)
            update();
    }

private:

    // Milkdrop presets can name an external texture - "PEcubesBW", "worms", "fw_clouds" - and
    // projectM looks for it only in the directories it has been given. We never gave it any, so
    // every one of those lookups failed on every machine: 2,237 of the 9,795 presets ask for at
    // least one, across 89 distinct names. Neither the preset pack nor projectM ships the files,
    // so this does not fix those presets by itself - it makes them fixable, by giving a
    // directory the textures can be dropped into.
    void applyTextureSearchPaths()
    {
        if (!m_pm)
            return;
        if (m_texturesPath.isEmpty()) {
            projectm_set_texture_search_paths(m_pm, nullptr, 0);
            return;
        }
        const QByteArray utf8 = m_texturesPath.toUtf8();
        const char *paths[] = {utf8.constData()};
        projectm_set_texture_search_paths(m_pm, paths, 1);
        qInfo("texture library: %s", qPrintable(m_texturesPath));
    }

    void loadLibrary()
    {
        if (!m_playlist || m_presetsPath.isEmpty())
            return;

        projectm_playlist_clear(m_playlist);

        // Presets measured to render nothing. The browser hides them too, but the rotation is
        // where it matters most: a hidden preset that still comes round leaves the user watching
        // a black stage with no name to look up and nothing to click.
        QSet<QString> blocked;
        if (!m_showBroken) {
            for (const QString &listPath : std::as_const(m_blocklists)) {
                if (listPath.isEmpty())
                    continue;
                QFile blockFile(listPath);
                if (!blockFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    qWarning("preset library: cannot read blocklist %s", qPrintable(listPath));
                    continue;
                }
                QTextStream in(&blockFile);
                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
                        blocked.insert(line);
                }
            }
        }
        const QDir libraryRoot(m_presetsPath);

        uint32_t added = 0;
        // An explicit list wins: this is how the category picker narrows the rotation.
        if (!m_explicitPaths.isEmpty()) {
            for (const QString &path : std::as_const(m_explicitPaths)) {
                if (projectm_playlist_add_preset(m_playlist, path.toUtf8().constData(), false))
                    ++added;
            }
            qInfo("preset library: %u presets from an explicit list", added);
            // The browser addresses presets by row, so its list and this playlist have to stay the
            // same length. A preset projectM declines to add shifts every index after it, which
            // shows up as clicking one row and getting another.
            if (added != uint32_t(m_explicitPaths.size()))
                qWarning("preset library: %d paths offered, %u accepted - row indices will not line up",
                         int(m_explicitPaths.size()), added);
        }

        // A curated list keeps the default experience to a few hundred presets. The full
        // corpus stays one menu item away rather than being the thing a new user lands in.
        if (added == 0 && !m_curatedList.isEmpty()) {
            QFile listFile(m_curatedList);
            if (listFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QDir base(m_presetsPath);
                QTextStream in(&listFile);
                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                        continue;
                    if (blocked.contains(line))
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
            // Walked here rather than handed to projectm_playlist_add_path, which takes a whole
            // directory tree and offers no way to leave anything out. Two things have to be left
            // out: the blocklist, and the "!" categories - transitions to black that the browser
            // has always excluded by name while the rotation, on this path, played them.
            uint32_t skipped = 0;
            QDirIterator it(m_presetsPath, {QStringLiteral("*.milk")}, QDir::Files,
                            QDirIterator::Subdirectories);
            QStringList found;
            while (it.hasNext())
                found.append(it.next());
            found.sort();
            for (const QString &full : std::as_const(found)) {
                const QString relative = libraryRoot.relativeFilePath(full);
                if (relative.startsWith(QLatin1Char('!')) || blocked.contains(relative)) {
                    ++skipped;
                    continue;
                }
                if (projectm_playlist_add_preset(m_playlist, full.toUtf8().constData(), false))
                    ++added;
            }
            qInfo("preset library: %u presets from %s (%u not shown)", added,
                  qPrintable(m_presetsPath), skipped);
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
        if (!m_playlist)
            return;

        if (m_pendingJumpIndex >= 0) {
            const uint32_t size = projectm_playlist_size(m_playlist);
            if (size > 0 && uint32_t(m_pendingJumpIndex) < size) {
                m_requestedPreset = m_pendingJumpIndex < m_explicitPaths.size()
                                        ? m_explicitPaths.at(m_pendingJumpIndex)
                                        : QString();
                g_presetCompileFailed.store(false);
                projectm_playlist_set_position(m_playlist, uint32_t(m_pendingJumpIndex), true);
            }
            else
                qWarning("preset jump to %d ignored: the playlist holds %u",
                         m_pendingJumpIndex, size);
            m_pendingJumpIndex = -1;
            m_presetDirty = true;
        }

        if (m_commands.isEmpty())
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
        QString file;
        if (count > 0) {
            if (char *item = projectm_playlist_item(m_playlist, index)) {
                file = QString::fromUtf8(item);
                name = QFileInfo(file).completeBaseName();
                projectm_playlist_free_string(item);
            }
        }
        QMetaObject::invokeMethod(m_item, "applyPreset", Qt::QueuedConnection,
                                  Q_ARG(QString, name), Q_ARG(QString, file),
                                  Q_ARG(int, int(index)), Q_ARG(int, int(count)));
    }

    QSize scaledSize(const QSize &itemSize) const
    {
        // Width is quantised so that a pixel or two of drift - a splitter nudge, a rounding
        // difference - does not rebuild the buffer for no visible gain. Height is derived from
        // the item's own aspect rather than quantised too: rounding both axes independently
        // would change the aspect ratio by up to a step and visibly stretch the picture.
        constexpr int kStep = 16;
        const int raw = qRound(itemSize.width() * m_renderScale);
        const int w = qMax(kStep, (raw + kStep / 2) / kStep * kStep);
        const int h = itemSize.width() > 0
                          ? qMax(16, qRound(double(w) * itemSize.height() / itemSize.width()))
                          : qMax(16, qRound(itemSize.height() * m_renderScale));
        return QSize(w, h);
    }

    void feedAudio()
    {
        float samples[kMaxFeedFrames * 2];

        // Only ever real audio. If the pipeline has not delivered any yet, feed silence
        // rather than inventing a signal.
        // Everything that has arrived since the last frame, so projectM sees a continuous
        // waveform. Bounded: after a stall, catching up on more than this is pointless and the
        // older audio no longer corresponds to anything on screen.
        const int got = m_ring ? m_ring->readContinuous(samples, kMaxFeedFrames) : 0;
        if (got > 0) {
            m_audioIsLive = true;
            m_emptyFeeds = 0;
            if (qEnvironmentVariableIsSet("PLAYER_PROBE") && m_frameCount % 60 == 0) {
                double sum = 0.0, peak = 0.0;
                for (int i = 0; i < got * 2; ++i) {
                    sum += double(samples[i]) * samples[i];
                    peak = qMax(peak, qAbs(double(samples[i])));
                }
                qInfo("  audio: LIVE  %d frames  rms %.4f  peak %.4f", got,
                      std::sqrt(sum / (got * 2)), peak);
            }
            projectm_pcm_add_float(m_pm, samples, got, PROJECTM_STEREO);
            return;
        }
        // Nothing new this frame. That is normal while playing - the renderer can easily run
        // faster than audio arrives - and feeding silence here would punch a hole in the
        // waveform, which is the exact fault this read model exists to remove. Feed nothing and
        // let projectM keep the buffer it has.
        if (++m_emptyFeeds < 15)
            return;

        // Genuinely quiet for a quarter of a second or so: now silence is the truth, and
        // feeding it lets the visualiser settle instead of freezing on its last buffer.
        m_audioIsLive = false;
        m_phase = 0.0;
        m_sweepPhase = 0.0;
        constexpr int kSilence = 512;
        std::memset(samples, 0, sizeof(float) * kSilence * 2);
        projectm_pcm_add_float(m_pm, samples, kSilence, PROJECTM_STEREO);
    }

    projectm_handle m_pm = nullptr;
    projectm_playlist_handle m_playlist = nullptr;
    ProjectMItem *m_item = nullptr;
    AudioRingBuffer *m_ring = nullptr;
    bool m_audioIsLive = false;
    QString m_presetsPath;
    QString m_texturesPath;
    QString m_curatedList;
    QStringList m_blocklists;
    bool m_showBroken = false;
    QStringList m_explicitPaths;
    int m_pendingJumpIndex = -1;
    QString m_requestedPreset;
    bool m_active = true;
    bool m_selfDriven = false;
    bool m_idleCleared = false;
    QList<int> m_commands;
    bool m_pendingLibrary = false;
    bool m_presetDirty = false;
    bool m_wantLocked = false;
    bool m_wantShuffle = true;
    qreal m_wantDuration = 30.0;
    bool m_yieldToDesktop = true;
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
        QOpenGLVertexArrayObject m_vao;
        bool m_fboChecked = false;
        QSet<GLenum> m_seenGlErrors;
    QString m_presetPath;
    bool m_pendingPreset = false;
    bool m_pendingTextures = false;
    QElapsedTimer m_statsPosted;
    double m_lastFrameMs = 0.0;
    double m_fps = 0.0;
    // What projectM was last told; presets read it and scale their animation by it.
    int m_reportedFps = 60;
    // Consecutive frames with no new audio; see feedAudio().
    int m_emptyFeeds = 0;
    double m_phase = 0.0;
    double m_sweepPhase = 0.0;
    int m_frameCount = 0;
    QElapsedTimer m_intervalTimer;
};

} // namespace

ProjectMItem::ProjectMItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    // Quality choices should survive a restart: a user who turned the visualiser down did so
    // for a reason, and making them do it again every launch is its own bug.
    QSettings settings;
    m_adaptiveQuality = settings.value(QStringLiteral("visual/adaptive"), true).toBool();
    m_renderScale = qBound(0.1, settings.value(QStringLiteral("visual/renderScale"), 0.5).toDouble(), 1.0);
    m_targetFps = qBound(15.0, settings.value(QStringLiteral("visual/targetFps"), 30.0).toDouble(), 60.0);

    // Slightly longer than the 160ms panel animation, so one rebuild happens at the end of a
    // collapse rather than one per animated frame.
    m_sizeSettleTimer.setSingleShot(true);
    m_sizeSettleTimer.setInterval(200);
    connect(&m_sizeSettleTimer, &QTimer::timeout, this, [this]() {
        const QSizeF now(width(), height());
        if (now == m_settledSize)
            return;
        m_settledSize = now;
        // synchronize() only runs when the item is marked dirty, so without this the settled
        // size would sit there unread until something else happened to dirty the item.
        update();
    });

    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setInterval(m_maxFps > 0 ? qMax(1, 1000 / m_maxFps) : 16);
    connect(&m_frameTimer, &QTimer::timeout, this, [this]() { update(); });

    // Render scale requires taking manual control of the texture size; with
    // textureFollowsItemSize the FBO would always match the item.
    setTextureFollowsItemSize(false);
    // projectM draws with an OpenGL bottom-left origin; Qt expects top-left.
    setMirrorVertically(true);
}

void ProjectMItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickFramebufferObject::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    // The very first sizing is adopted at once; there is nothing on screen to protect yet and
    // waiting would just show a 16x16 buffer stretched across the stage.
    if (m_settledSize.isEmpty()) {
        m_settledSize = newGeometry.size();
        update();
        return;
    }
    m_sizeSettleTimer.start();
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
    // Only a deliberate choice is worth storing. While the adaptive scaler is running this
    // changes constantly and persisting every step would just thrash the settings file.
    if (!m_adaptiveQuality)
        QSettings().setValue(QStringLiteral("visual/renderScale"), m_renderScale);
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

void ProjectMItem::setTexturesPath(const QString &path)
{
    if (path == m_texturesPath)
        return;
    m_texturesPath = path;
    emit texturesPathChanged();
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

void ProjectMItem::setBlocklists(const QStringList &paths)
{
    if (paths == m_blocklists)
        return;
    m_blocklists = paths;
    emit blocklistChanged();
    update();
}

void ProjectMItem::setShowBroken(bool show)
{
    if (show == m_showBroken)
        return;
    m_showBroken = show;
    emit blocklistChanged();
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

void ProjectMItem::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (m_active && m_maxFps > 0)
        m_frameTimer.start();
    else
        m_frameTimer.stop();
    emit activeChanged();
    update();
}

void ProjectMItem::setYieldToDesktop(bool yield)
{
    if (yield == m_yieldToDesktop)
        return;
    m_yieldToDesktop = yield;
    emit yieldToDesktopChanged();
    // Apply it here rather than waiting for the next render(): when video is playing this item
    // is invisible and render() will never come.
    syncScheduling();
    update();
}

void ProjectMItem::publishRenderThread(int tid)
{
    QMutexLocker lock(&m_schedMutex);
    m_renderTid = tid;
}

// SCHED_BATCH rather than a nice value, deliberately. Renicing is one-way for an unprivileged
// process - RLIMIT_NICE is 0 on the target distros, so a thread lowered to nice 5 can never be
// raised back - and that would permanently penalise video rendering, which shares this very
// render thread. SCHED_BATCH costs no CPU share, only the wakeup-latency bonus that lets a busy
// renderer out-compete the panel and the window manager, and it can be switched off again.
void ProjectMItem::syncScheduling()
{
#ifdef Q_OS_LINUX
    QMutexLocker lock(&m_schedMutex);
    const int wanted = m_yieldToDesktop ? 1 : 0;
    if (wanted == m_batchState)
        return;
    // Nothing to reschedule until the render thread has announced itself.
    if (m_renderTid == 0)
        return;

    if (qEnvironmentVariableIsSet("PLAYER_NO_SCHED_BATCH")) {
        m_batchState = wanted;
        return;
    }

    sched_param param{};
    param.sched_priority = 0;
    const int policy = wanted ? SCHED_BATCH : SCHED_OTHER;

    int changed = 0;
    if (sched_setscheduler(static_cast<pid_t>(m_renderTid), policy, &param) == 0)
        ++changed;

    // The rasterisation happens on Mesa's worker threads, not on the render thread. They belong
    // to this process, so they are ours to reschedule; missing them leaves the desktop competing
    // with four busy workers.
    QDir tasks(QStringLiteral("/proc/self/task"));
    const QStringList entries = tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QFile comm(QStringLiteral("/proc/self/task/%1/comm").arg(entry));
        if (!comm.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        if (!QString::fromUtf8(comm.readAll()).trimmed().startsWith(QStringLiteral("llvmpipe")))
            continue;
        if (sched_setscheduler(entry.toInt(), policy, &param) == 0)
            ++changed;
    }

    qCDebug(lcRender, "rendering threads set to %s (%d threads)",
          wanted ? "SCHED_BATCH" : "SCHED_OTHER", changed);
    m_batchState = wanted;
#endif
}

void ProjectMItem::setMaxFps(int fps)
{
    fps = fps <= 0 ? 0 : qBound(10, fps, 240);
    if (fps == m_maxFps)
        return;
    m_maxFps = fps;
    if (m_maxFps > 0) {
        m_frameTimer.setInterval(qMax(1, 1000 / m_maxFps));
        if (m_active)
            m_frameTimer.start();
    } else {
        m_frameTimer.stop();
    }
    emit maxFpsChanged();
    update();
}

void ProjectMItem::setPresetList(const QStringList &paths)
{
    QMutexLocker lock(&m_commandMutex);
    if (paths == m_presetList)
        return;
    m_presetList = paths;
    lock.unlock();
    update();
}

QStringList ProjectMItem::presetList() const
{
    QMutexLocker lock(const_cast<QMutex *>(&m_commandMutex));
    return m_presetList;
}

void ProjectMItem::jumpTo(int index)
{
    QMutexLocker lock(&m_commandMutex);
    m_pendingJump = index;
    lock.unlock();
    update();
}

int ProjectMItem::takePendingJump()
{
    QMutexLocker lock(&m_commandMutex);
    const int jump = m_pendingJump;
    m_pendingJump = -1;
    return jump;
}

void ProjectMItem::setAdaptiveQuality(bool enabled)
{
    if (enabled == m_adaptiveQuality)
        return;
    m_adaptiveQuality = enabled;
    QSettings().setValue(QStringLiteral("visual/adaptive"), enabled);
    if (!enabled)
        QSettings().setValue(QStringLiteral("visual/renderScale"), m_renderScale);
    emit adaptiveQualityChanged();
    update();
}

void ProjectMItem::setTargetFps(qreal fps)
{
    fps = qBound(15.0, fps, 60.0);
    if (qFuzzyCompare(fps, m_targetFps))
        return;
    m_targetFps = fps;
    QSettings().setValue(QStringLiteral("visual/targetFps"), fps);
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

void ProjectMItem::reportPresetFailed(const QString &name)
{
    qWarning("visualisation %s could not be loaded", qUtf8Printable(name));
    emit presetLoadFailed(name);
}

void ProjectMItem::applyPreset(const QString &name, const QString &file, int index, int count)
{
    m_presetName = name;
    m_presetFile = file;
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

