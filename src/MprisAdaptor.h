#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class AudioEngine;
class PlaylistModel;

// MPRIS2 exists here for one reason: media keys. Desktop environments route XF86Audio* keys
// to whichever application owns an org.mpris.MediaPlayer2.* name, so this is the supported
// way to get them without grabbing keys globally.
class MprisPlayer : public QObject
{
    Q_OBJECT

public:
    MprisPlayer(AudioEngine *engine, PlaylistModel *playlist, QObject *parent = nullptr);

    bool registerService();

    AudioEngine *engine() const { return m_engine; }
    PlaylistModel *playlist() const { return m_playlist; }

    QString playbackStatus() const;
    QVariantMap metadata() const;

signals:
    void nextRequested();
    void previousRequested();
    void raiseRequested();
    void quitRequested();

private slots:
    void emitPlaybackStatusChanged();
    void emitMetadataChanged();

private:
    void emitPropertiesChanged(const QVariantMap &changed);

    AudioEngine *m_engine = nullptr;
    PlaylistModel *m_playlist = nullptr;
};

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(MprisPlayer *parent);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Player"); }
    QString desktopEntry() const { return QStringLiteral("player"); }
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Raise();
    void Quit();

private:
    MprisPlayer *m_owner;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qint64 Position READ position)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayerAdaptor(MprisPlayer *parent);

    QString playbackStatus() const;
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qint64 position() const;
    double rate() const { return 1.0; }
    void setRate(double) {}
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qint64 offsetMicroseconds);
    void SetPosition(const QDBusObjectPath &trackId, qint64 positionMicroseconds);
    void OpenUri(const QString &uri);

signals:
    void Seeked(qint64 positionMicroseconds);

private:
    MprisPlayer *m_owner;
};
