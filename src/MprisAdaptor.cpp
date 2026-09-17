#include "MprisAdaptor.h"

#include "AudioEngine.h"
#include "PlaylistModel.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>

namespace {
constexpr const char *kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char *kServiceName = "org.mpris.MediaPlayer2.reverie";
}

MprisPlayer::MprisPlayer(AudioEngine *engine, PlaylistModel *playlist, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_playlist(playlist)
{
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);
    new ReverieAppAdaptor(this);

    if (m_engine) {
        connect(m_engine, &AudioEngine::stateChanged, this, &MprisPlayer::emitPlaybackStatusChanged);
        connect(m_engine, &AudioEngine::durationChanged, this, &MprisPlayer::emitMetadataChanged);
    }
    if (m_playlist)
        connect(m_playlist, &PlaylistModel::currentIndexChanged, this, &MprisPlayer::emitMetadataChanged);
}

const char *MprisPlayer::serviceName() { return kServiceName; }
const char *MprisPlayer::objectPath() { return kObjectPath; }
const char *MprisPlayer::appInterface() { return "io.github.sandrewd.ReverieMediaPlayer"; }

bool MprisPlayer::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qInfo("MPRIS: no session bus; media keys will not be available");
        return false;
    }
    if (!bus.registerObject(QLatin1String(kObjectPath), this)) {
        qWarning("MPRIS: failed to register object at %s", kObjectPath);
        return false;
    }
    if (!bus.registerService(QLatin1String(kServiceName))) {
        // A second instance is not an error worth failing over; it just does not own the keys.
        qInfo("MPRIS: could not take %s, another instance probably owns it", kServiceName);
        return false;
    }
    return true;
}

QString MprisPlayer::playbackStatus() const
{
    if (!m_engine)
        return QStringLiteral("Stopped");
    switch (m_engine->state()) {
    case AudioEngine::Playing:
    case AudioEngine::Buffering:
        return QStringLiteral("Playing");
    case AudioEngine::Paused:
        return QStringLiteral("Paused");
    default:
        return QStringLiteral("Stopped");
    }
}

QVariantMap MprisPlayer::metadata() const
{
    QVariantMap map;
    if (!m_playlist || m_playlist->currentIndex() < 0) {
        map.insert(QStringLiteral("mpris:trackid"),
                   QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack")));
        return map;
    }

    map.insert(QStringLiteral("mpris:trackid"),
               QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Track/%1")
                                                       .arg(m_playlist->currentIndex()))));
    map.insert(QStringLiteral("xesam:title"), m_playlist->currentTitle());
    const QString artist = m_playlist->currentArtist();
    if (!artist.isEmpty())
        map.insert(QStringLiteral("xesam:artist"), QStringList{artist});
    map.insert(QStringLiteral("xesam:url"), m_playlist->currentPath());
    if (m_engine && m_engine->duration() > 0)
        map.insert(QStringLiteral("mpris:length"), qint64(m_engine->duration()) * 1000);
    return map;
}

void MprisPlayer::emitPropertiesChanged(const QVariantMap &changed)
{
    QDBusMessage signal = QDBusMessage::createSignal(
        QLatin1String(kObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

void MprisPlayer::emitPlaybackStatusChanged()
{
    emitPropertiesChanged({{QStringLiteral("PlaybackStatus"), playbackStatus()}});
}

void MprisPlayer::emitMetadataChanged()
{
    emitPropertiesChanged({{QStringLiteral("Metadata"), metadata()}});
}

// --- root interface ---------------------------------------------------------------------

MprisRootAdaptor::MprisRootAdaptor(MprisPlayer *parent)
    : QDBusAbstractAdaptor(parent)
    , m_owner(parent)
{
    setAutoRelaySignals(true);
}

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")};
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"),
            QStringLiteral("audio/ogg"),  QStringLiteral("audio/x-vorbis+ogg"),
            QStringLiteral("audio/mp4"),  QStringLiteral("audio/x-wav")};
}

void MprisRootAdaptor::Raise() { emit m_owner->raiseRequested(); }

void MprisRootAdaptor::Quit() { emit m_owner->quitRequested(); }

// --- player interface -------------------------------------------------------------------

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisPlayer *parent)
    : QDBusAbstractAdaptor(parent)
    , m_owner(parent)
{
    setAutoRelaySignals(true);
}

QString MprisPlayerAdaptor::playbackStatus() const { return m_owner->playbackStatus(); }

QVariantMap MprisPlayerAdaptor::metadata() const { return m_owner->metadata(); }

double MprisPlayerAdaptor::volume() const
{
    return m_owner->engine() ? m_owner->engine()->volume() : 0.0;
}

void MprisPlayerAdaptor::setVolume(double volume)
{
    if (m_owner->engine())
        m_owner->engine()->setVolume(volume);
}

qint64 MprisPlayerAdaptor::position() const
{
    return m_owner->engine() ? qint64(m_owner->engine()->position()) * 1000 : 0;
}

bool MprisPlayerAdaptor::canGoNext() const
{
    return m_owner->playlist() && m_owner->playlist()->rowCount() > 0;
}

bool MprisPlayerAdaptor::canGoPrevious() const { return canGoNext(); }

bool MprisPlayerAdaptor::canPlay() const { return canGoNext(); }

bool MprisPlayerAdaptor::canPause() const
{
    return m_owner->engine() && m_owner->engine()->state() == AudioEngine::Playing;
}

bool MprisPlayerAdaptor::canSeek() const
{
    return m_owner->engine() && m_owner->engine()->seekable();
}

void MprisPlayerAdaptor::Next() { emit m_owner->nextRequested(); }

void MprisPlayerAdaptor::Previous() { emit m_owner->previousRequested(); }

void MprisPlayerAdaptor::Pause()
{
    if (m_owner->engine())
        m_owner->engine()->pause();
}

void MprisPlayerAdaptor::PlayPause()
{
    if (m_owner->engine())
        m_owner->engine()->togglePlayPause();
}

void MprisPlayerAdaptor::Stop()
{
    if (m_owner->engine())
        m_owner->engine()->stop();
}

void MprisPlayerAdaptor::Play()
{
    if (m_owner->engine())
        m_owner->engine()->play();
}

void MprisPlayerAdaptor::Seek(qint64 offsetMicroseconds)
{
    if (!m_owner->engine())
        return;
    const qint64 target = m_owner->engine()->position() + offsetMicroseconds / 1000;
    m_owner->engine()->seek(qMax(qint64(0), target));
    emit Seeked(qMax(qint64(0), target) * 1000);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &, qint64 positionMicroseconds)
{
    if (m_owner->engine())
        m_owner->engine()->seek(positionMicroseconds / 1000);
}

void MprisPlayerAdaptor::OpenUri(const QString &uri)
{
    // Goes through the playlist rather than straight to the engine. Setting the source directly
    // left audio playing with no row current, which is the orphan case the playlist invariant
    // exists to prevent: clearing the list or removing the track would not have stopped it.
    emit m_owner->openFilesRequested(QStringList{uri});
}

ReverieAppAdaptor::ReverieAppAdaptor(MprisPlayer *parent)
    : QDBusAbstractAdaptor(parent)
    , m_owner(parent)
{
}

void ReverieAppAdaptor::OpenFiles(const QStringList &uris)
{
    if (!uris.isEmpty())
        emit m_owner->openFilesRequested(uris);
}
