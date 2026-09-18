#pragma once

#include <QObject>
#include <QVariantList>
#include <QStringList>
#include <QList>
// Included rather than forward-declared: a Q_INVOKABLE taking QQuickItem* needs its metatype,
// and a forward declaration collides with the one QtQuick declares later.
#include <QImage>
#include <QQuickItem>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "AudioRingBuffer.h"

typedef struct _GstElement GstElement;
typedef struct _GstStreamCollection GstStreamCollection;

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
    // Whether the current media actually carries a video stream. playbin3 has no n-video
    // property; this comes from the stream collection it publishes on the bus.
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)

    // Subtitles. playbin3 already renders these into the video frames before they reach our
    // sink - verified, not assumed - so nothing here draws text. What it does is choose which
    // text stream is selected, which is the only part the user was missing: playbin3 turns the
    // first one on by default and offered no way to turn it off.
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY subtitlesChanged)
    // Index into subtitleTracks, or -1 for off.
    Q_PROPERTY(int subtitleTrack READ subtitleTrack NOTIFY subtitlesChanged)
    // The preference, which outlives any one file. subtitleTrack is what is on right now.
    Q_PROPERTY(bool subtitlesEnabled READ subtitlesEnabled NOTIFY subtitlesChanged)

    // Audio tracks, the same mechanism as subtitles. A film with a commentary track or two
    // languages has several, and playbin3 picks one with no way to change it.
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY audioTracksChanged)
    Q_PROPERTY(int audioTrack READ audioTrack NOTIFY audioTracksChanged)
    // ISO 639-1 code, or empty for "follow the system locale". Applied whenever a file turns
    // out to have more than one audio track, so the choice does not have to be made per file.
    Q_PROPERTY(QString preferredAudioLanguage READ preferredAudioLanguage
                   WRITE setPreferredAudioLanguage NOTIFY preferredAudioLanguageChanged)
    // Same idea for subtitles: which track to turn on when a file has several. It does not
    // decide whether subtitles are on at all - that is the separate on/off preference.
    Q_PROPERTY(QString preferredSubtitleLanguage READ preferredSubtitleLanguage
                   WRITE setPreferredSubtitleLanguage NOTIFY preferredSubtitleLanguageChanged)

    // Tags as the decoder reports them, for any medium. TagLib stays authoritative for files -
    // see the playlist - but it reads nothing from a file that carries no tags it understands,
    // and the decoder often does. These fill that gap rather than override anything.
    Q_PROPERTY(QString tagTitle READ tagTitle NOTIFY tagsChanged)
    Q_PROPERTY(QString tagArtist READ tagArtist NOTIFY tagsChanged)

    // Equaliser. Ten bands from 29 Hz to 15 kHz, which is what equalizer-10bands offers, sitting
    // between the decode and the tee so the visualiser reacts to what you actually hear.
    Q_PROPERTY(bool equaliserEnabled READ equaliserEnabled WRITE setEqualiserEnabled
                   NOTIFY equaliserChanged)
    // The preset in use, or an empty string when the bands have been edited by hand.
    Q_PROPERTY(QString equaliserPreset READ equaliserPreset NOTIFY equaliserChanged)
    Q_PROPERTY(QVariantList equaliserBands READ equaliserBands NOTIFY equaliserChanged)
    Q_PROPERTY(QStringList equaliserPresetNames READ equaliserPresetNames CONSTANT)

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
    bool hasVideo() const { return m_hasVideo; }

    // Hands the QML video surface to the sink. Must be a Qt6GLVideoItem from the qml6 plugin;
    // the sink stores it as a plain QQuickItem pointer and will not accept anything else.
    Q_INVOKABLE void setVideoItem(QQuickItem *item);

    QVariantList subtitleTracks() const;
    int subtitleTrack() const { return m_subtitleTrack; }
    bool subtitlesEnabled() const { return m_subtitlesWanted; }
    // -1 turns subtitles off. The choice is remembered across tracks as a preference, so a
    // user who turned them off does not have to do it again for every file.
    Q_INVOKABLE void setSubtitleTrack(int index);
    // On/off as the user thinks of it. "On" means the preferred language if the file has it,
    // otherwise the first track - never a fixed index. Works with nothing playing, in which
    // case it only records the preference for the next file.
    Q_INVOKABLE void setSubtitlesEnabled(bool on);
    // Attaches an external subtitle file to whatever is playing. playbin3 ignores `suburi`
    // unless it is set below PAUSED, so this cycles the pipeline through READY and seeks back -
    // roughly 800ms, which is why it is only ever done on an explicit request.
    Q_INVOKABLE bool addSubtitleFile(const QString &path);

    QVariantList audioTracks() const;
    int audioTrack() const { return m_audioTrack; }
    Q_INVOKABLE void setAudioTrack(int index);
    QString preferredAudioLanguage() const { return m_preferredAudioLanguage; }
    void setPreferredAudioLanguage(const QString &code);
    // The languages offered in the menus, as {code, label}. Nothing is derived from the media:
    // the preference has to be settable before anything is loaded. The empty code means
    // different things for the two, so the first entry is labelled accordingly.
    Q_INVOKABLE QVariantList languageChoices(bool subtitles) const;
    QString preferredSubtitleLanguage() const { return m_preferredSubtitleLanguage; }
    void setPreferredSubtitleLanguage(const QString &code);

    // Fixed by equalizer-10bands; not a preference.
    static constexpr int kEqualiserBands = 10;

    QString tagTitle() const { return m_tagTitle; }
    QString tagArtist() const { return m_tagArtist; }

    bool equaliserEnabled() const { return m_equaliserEnabled; }
    void setEqualiserEnabled(bool enabled);
    QString equaliserPreset() const { return m_equaliserPreset; }
    QVariantList equaliserBands() const;
    QStringList equaliserPresetNames() const;
    // Band centre frequencies, for labelling the custom mixer. Fixed by the element.
    Q_INVOKABLE QStringList equaliserBandLabels() const;
    Q_INVOKABLE void applyEqualiserPreset(const QString &name);
    Q_INVOKABLE void setEqualiserBand(int band, qreal gainDb);
    Q_INVOKABLE void resetEqualiser();
    // These two exist so the menu's exclusive group survives re-selecting the active entry.
    // A MenuItem's `checked` binding is detached the moment QML assigns to it on click, and it
    // only comes back when the property it was bound to signals a change - so an item whose
    // handler is a no-op unchecks itself and the group falls to whatever else matches. Both
    // emit unconditionally for that reason.
    Q_INVOKABLE void chooseNoEqualiser();
    Q_INVOKABLE void applyCustomEqualiser();
    // Zeroes the bands without leaving the custom curve. Calling applyEqualiserPreset("Flat")
    // from inside the custom editor silently moved the selection to the Flat *preset*, so
    // flattening dropped you out of the mode you were editing in - reported as confusing, and it
    // was: every other equaliser treats this as "reset my curve".
    Q_INVOKABLE void flattenCustomEqualiser();
    // Called from the streaming thread; forwards to the item on the GUI thread.
    void deliverVideoFrame(const QImage &frame);

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
    void hasVideoChanged();
    void subtitlesChanged();
    void audioTracksChanged();
    void preferredAudioLanguageChanged();
    void preferredSubtitleLanguageChanged();
    void equaliserChanged();
    void tagsChanged();
    void endOfStream();
    void errorOccurred(const QString &message);

private:
    void buildPipeline();
    void buildVideoSink();
    void pollBus();
    void pollPosition();
    void setState(State state);
    void updateDuration();
    void updateBuffering();

    // Pushes our volume and mute onto the pipeline unconditionally. The setters only act on a
    // change, so at startup - m_muted already false - nothing was ever sent, and PulseAudio and
    // PipeWire restore a *per-application* mute and volume onto every new stream. One accidental
    // mute is then remembered for ever: the server silences the stream while our slider sits at
    // 70% and the mute button looks off. Asserting our state makes the app authoritative again.
    void applyOutputLevels();
    void applyEqualiserToPipeline();
    void saveEqualiser();

    GstElement *m_equaliser = nullptr;
    // Attenuates by the largest positive band gain. A +12 dB boost on material already near full
    // scale would clip, and the fix every player uses is a pre-amp - but §1 says remove knobs
    // rather than add them, so this one is derived from the curve instead of exposed.
    GstElement *m_makeupGain = nullptr;
    QString m_tagTitle;
    QString m_tagArtist;
    bool m_equaliserEnabled = false;
    QString m_equaliserPreset;
    QList<qreal> m_equaliserGains;

    GstElement *m_pipeline = nullptr;
    GstElement *m_videoSink = nullptr;
    class VideoItem *m_videoItem = nullptr;
    bool m_hasVideo = false;

    struct SubtitleTrack {
        QString id;
        QString language;
        QString title;
        bool external = false;
    };
    QVector<SubtitleTrack> m_subtitles;
    // Audio streams carry the same three fields, so the struct is shared.
    QVector<SubtitleTrack> m_audioStreams;
    int m_audioTrack = -1;
    QString m_preferredAudioLanguage;
    QString m_preferredSubtitleLanguage;
    void rebuildAudioTracks(GstStreamCollection *collection);
    QString effectiveAudioLanguage() const;
    int trackForLanguage(const QVector<SubtitleTrack> &tracks, const QString &code) const;
    // Video ids only. Audio and text are chosen, so they cannot live in the keep-list.
    QStringList m_videoStreamIds;
    int m_subtitleTrack = -1;
    // The preference, distinct from the index: which track is number 0 changes per file.
    bool m_subtitlesWanted = true;
    // Set when an external file has just been attached, so the text stream it adds is selected
    // as soon as the collection naming it arrives.
    QString m_subtitleFile;
    bool m_selectNewSubtitle = false;
    void rebuildSubtitleTracks(GstStreamCollection *collection);
    void applySubtitleSelection();
    QString sidecarSubtitleFor(const QString &uri) const;
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
