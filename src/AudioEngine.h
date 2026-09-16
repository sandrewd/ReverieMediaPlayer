#pragma once

#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "AudioRingBuffer.h"

typedef struct _GstElement GstElement;

// One playbin3. Network streams, local files and the tags that come with either arrive
// through the same path, so stream support later is a dialog and a list, not an engine.
class AudioEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    // Tags as the pipeline reports them. For a local file TagLib is authoritative and these
    // are ignored; for an Icecast stream these are the only metadata there is.
    Q_PROPERTY(QString streamTitle READ streamTitle NOTIFY streamTitleChanged)
    // The station's own name, as announced over ICY. Distinct from whatever the user called
    // the entry when they added it.
    Q_PROPERTY(QString streamStation READ streamStation NOTIFY streamStationChanged)
    // Buffering is not a playback state: the stream is still playing while it tops up, and
    // folding it into `state` made the transport show a play button during playback.
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)

public:
    enum State { Stopped, Playing, Paused, Buffering };
    Q_ENUM(State)

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    State state() const { return m_state; }
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }
    qreal volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    bool seekable() const { return m_seekable; }
    QString source() const { return m_source; }
    QString streamTitle() const { return m_streamTitle; }
    QString streamStation() const { return m_streamStation; }
    bool buffering() const { return m_buffering; }

    void setVolume(qreal volume);
    void setMuted(bool muted);

    // Shared with the visualizer, which pulls the newest samples on its own thread.
    AudioRingBuffer *ringBuffer() { return m_ring.get(); }

public slots:
    void setSource(const QString &uriOrPath);
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(qint64 milliseconds);

signals:
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void seekableChanged();
    void sourceChanged();
    void streamTitleChanged();
    void streamStationChanged();
    void bufferingChanged();
    void endOfStream();
    void errorOccurred(const QString &message);

private:
    void buildPipeline();
    void pollBus();
    void pollPosition();
    void setState(State state);
    void updateDuration();
    void updateBuffering();

    GstElement *m_pipeline = nullptr;
    GstElement *m_appsink = nullptr;
    std::unique_ptr<AudioRingBuffer> m_ring;
    QTimer m_busTimer;
    QTimer m_positionTimer;

    State m_state = Stopped;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    qreal m_volume = 0.7;
    bool m_muted = false;
    bool m_seekable = false;
    QString m_source;
    QString m_streamTitle;
    QString m_streamStation;
    bool m_buffering = false;
    int m_bufferPercent = 100;
    QElapsedTimer m_sinceProgress;
};
