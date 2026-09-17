#pragma once

#include <QQuickFramebufferObject>
#include <QMutex>
#include <QTimer>
#include <QStringList>
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

    // Preset browsing. The corpus is ~9,800 presets, so this is a cursor over a library
    // rather than a list the user is ever shown in full.
    Q_PROPERTY(QString presetsPath READ presetsPath WRITE setPresetsPath NOTIFY presetsPathChanged)
    Q_PROPERTY(QString curatedList READ curatedList WRITE setCuratedList NOTIFY curatedListChanged)
    Q_PROPERTY(QString presetName READ presetName NOTIFY presetChanged)
    Q_PROPERTY(int presetIndex READ presetIndex NOTIFY presetChanged)
    Q_PROPERTY(int presetCount READ presetCount NOTIFY presetChanged)
    Q_PROPERTY(bool presetLocked READ presetLocked WRITE setPresetLocked NOTIFY presetLockedChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(qreal presetDuration READ presetDuration WRITE setPresetDuration NOTIFY presetDurationChanged)

    // The frame-time overlay was always meant to become the input to automatic render-scale
    // selection rather than just a readout. This is that.
    // When nothing is playing the visualiser is black. No synthetic audio, no idle animation:
    // an animating visualiser with silent speakers reads as "music is playing" and is a lie.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool adaptiveQuality READ adaptiveQuality WRITE setAdaptiveQuality NOTIFY adaptiveQualityChanged)
    Q_PROPERTY(qreal targetFps READ targetFps WRITE setTargetFps NOTIFY targetFpsChanged)
    // Hard ceiling on how often we redraw. Without it the visualiser free-runs whenever a
    // preset is cheap, burning cores to produce frames nobody asked for - and on a software
    // renderer that starves the rest of the desktop. 0 means uncapped, for benchmarking.
    Q_PROPERTY(int maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)
    // Run the rendering threads as a batch workload so they stop out-competing the desktop
    // for wakeups. Must be switchable: video has presentation deadlines and should not be
    // scheduled this way. See the note in the brief.
    Q_PROPERTY(bool yieldToDesktop READ yieldToDesktop WRITE setYieldToDesktop NOTIFY yieldToDesktopChanged)

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

    QString presetsPath() const { return m_presetsPath; }
    void setPresetsPath(const QString &path);
    QString curatedList() const { return m_curatedList; }
    void setCuratedList(const QString &path);
    QString presetName() const { return m_presetName; }
    int presetIndex() const { return m_presetIndex; }
    int presetCount() const { return m_presetCount; }
    bool presetLocked() const { return m_presetLocked; }
    void setPresetLocked(bool locked);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    qreal presetDuration() const { return m_presetDuration; }
    void setPresetDuration(qreal seconds);

    bool active() const { return m_active; }
    void setActive(bool active);
    bool adaptiveQuality() const { return m_adaptiveQuality; }
    void setAdaptiveQuality(bool enabled);
    qreal targetFps() const { return m_targetFps; }
    void setTargetFps(qreal fps);
    int maxFps() const { return m_maxFps; }
    void setMaxFps(int fps);
    bool yieldToDesktop() const { return m_yieldToDesktop; }
    void setYieldToDesktop(bool yield);

    // Scheduling lives on the item, not the renderer, because it has to be reachable when the
    // renderer is not running. While video is on screen this item is invisible, so the scene
    // graph never calls render() - and a render thread left on SCHED_BATCH from the previous
    // audio track would go on penalising video, which is exactly what the property exists to
    // prevent. Safe from either thread: it locks, and both policies are unprivileged.
    void syncScheduling();
    // Called from synchronize(), which runs on the render thread with the GUI thread blocked.
    void publishRenderThread(int tid);

    Q_INVOKABLE void nextPreset();
    Q_INVOKABLE void previousPreset();
    Q_INVOKABLE void randomPreset();

    // Point the visualiser at an explicit set of presets, such as one category, and optionally
    // jump straight to one of them.
    Q_INVOKABLE void setPresetList(const QStringList &paths);
    Q_INVOKABLE void jumpTo(int index);

    QStringList presetList() const;
    int takePendingJump();

    // Drained by the renderer, which is the only thread allowed to touch projectM.
    enum Command { CommandNext = 1, CommandPrevious, CommandRandom, CommandReload };
    QList<int> takeCommands();

    qreal frameTimeMs() const { return m_frameTimeMs; }
    qreal fps() const { return m_fps; }
    QSize renderSize() const { return m_renderSize; }

public slots:
    // Both called from the render thread via a queued connection, so they land on the GUI thread.
    void applyStats(qreal frameTimeMs, qreal fps, int width, int height);
    void applyPreset(const QString &name, int index, int count);
    void applyAdaptiveScale(qreal scale);

signals:
    void renderScaleChanged();
    void presetPathChanged();
    void audioEngineChanged();
    void presetsPathChanged();
    void curatedListChanged();
    void presetChanged();
    void presetLockedChanged();
    void shuffleChanged();
    void presetDurationChanged();
    void activeChanged();
    void adaptiveQualityChanged();
    void targetFpsChanged();
    void maxFpsChanged();
    void yieldToDesktopChanged();
    void statsChanged();

private:
    qreal m_renderScale = 0.5;
    QString m_presetPath;
    AudioEngine *m_audioEngine = nullptr;
    qreal m_frameTimeMs = 0.0;
    qreal m_fps = 0.0;
    QSize m_renderSize;

    QString m_presetsPath;
    QString m_curatedList;
    QString m_presetName;
    int m_presetIndex = 0;
    int m_presetCount = 0;
    bool m_presetLocked = false;
    bool m_shuffle = true;
    qreal m_presetDuration = 30.0;

    bool m_active = true;
    bool m_adaptiveQuality = true;
    int m_maxFps = 60;
    bool m_yieldToDesktop = true;
    QMutex m_schedMutex;
    int m_renderTid = 0;
    int m_batchState = -1; // -1 unknown, 0 SCHED_OTHER, 1 SCHED_BATCH
    QTimer m_frameTimer;
    qreal m_targetFps = 30.0;

    QMutex m_commandMutex;
    QList<int> m_commands;
    QStringList m_presetList;
    int m_pendingJump = -1;
};
