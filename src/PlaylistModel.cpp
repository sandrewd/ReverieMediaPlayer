#include "PlaylistModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QRandomGenerator>
#include <QTextStream>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

namespace {

const QStringList kSupportedSuffixes = {
    // Audio
    QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("ogg"),
    QStringLiteral("oga"),  QStringLiteral("opus"), QStringLiteral("m4a"),
    QStringLiteral("aac"),  QStringLiteral("wav"),  QStringLiteral("wma"),
    QStringLiteral("aiff"), QStringLiteral("ape"),  QStringLiteral("mpc"),
    // Video. A media player that silently refuses to add a video file is worse than one that
    // adds it and cannot decode it: playbin3 will say so, whereas this said nothing at all.
    QStringLiteral("mp4"),  QStringLiteral("m4v"),  QStringLiteral("webm"),
    QStringLiteral("mkv"),  QStringLiteral("avi"),  QStringLiteral("mov"),
    QStringLiteral("ogv"),  QStringLiteral("wmv"),  QStringLiteral("flv"),
    QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("ts"),
    QStringLiteral("m2ts"), QStringLiteral("3gp"),
};

QString formatDuration(int seconds)
{
    if (seconds <= 0)
        return QStringLiteral("--:--");
    const int minutes = seconds / 60;
    if (minutes >= 60)
        return QStringLiteral("%1:%2:%3")
            .arg(minutes / 60)
            .arg(minutes % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings;
    m_persist = settings.value(QStringLiteral("playlist/persist"), false).toBool();
    m_repeatMode = static_cast<RepeatMode>(
        qBound(0, settings.value(QStringLiteral("playlist/repeatMode"), 0).toInt(), 2));
    m_shuffle = settings.value(QStringLiteral("playlist/shuffle"), false).toBool();
    if (m_persist)
        restoreSession();
}

PlaylistModel::~PlaylistModel()
{
    if (m_persist)
        saveSession();
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return {};

    const Track &track = m_tracks.at(index.row());
    switch (role) {
    case PathRole:         return track.path;
    case TitleRole:        return track.title;
    case ArtistRole:       return track.artist;
    case AlbumRole:        return track.album;
    case DurationRole:     return track.durationSec;
    case DurationTextRole: return track.isStream ? QStringLiteral("live")
                                                 : formatDuration(track.durationSec);
    case IsCurrentRole:    return index.row() == m_currentIndex;
    case IsStreamRole:     return track.isStream;
    case FileNameRole:     return track.isStream ? track.path
                                                 : QFileInfo(track.path).fileName();
    default:               return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        {PathRole, "path"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {DurationRole, "duration"},
        {DurationTextRole, "durationText"},
        {IsCurrentRole, "isCurrent"},
        {IsStreamRole, "isStream"},
        {FileNameRole, "fileName"},
    };
}

void PlaylistModel::setCurrentIndex(int index)
{
    if (index == m_currentIndex)
        return;
    const int previous = m_currentIndex;
    m_currentIndex = index;

    // Only the two affected rows need repainting, not the whole list.
    if (previous >= 0 && previous < m_tracks.size()) {
        const QModelIndex idx = createIndex(previous, 0);
        emit dataChanged(idx, idx, {IsCurrentRole});
    }
    if (m_currentIndex >= 0 && m_currentIndex < m_tracks.size()) {
        const QModelIndex idx = createIndex(m_currentIndex, 0);
        emit dataChanged(idx, idx, {IsCurrentRole});
    }
    emit currentIndexChanged();
}

void PlaylistModel::setPersistAcrossLaunches(bool persist)
{
    if (persist == m_persist)
        return;
    m_persist = persist;

    QSettings settings;
    settings.setValue(QStringLiteral("playlist/persist"), persist);

    // Turning the toggle off should not leave a copy of the playlist on disk.
    if (persist)
        saveSession();
    else
        QFile::remove(sessionFilePath());

    emit persistAcrossLaunchesChanged();
}

QString PlaylistModel::currentTitle() const
{
    return (m_currentIndex >= 0 && m_currentIndex < m_tracks.size())
        ? m_tracks.at(m_currentIndex).title : QString();
}

QString PlaylistModel::currentArtist() const
{
    return (m_currentIndex >= 0 && m_currentIndex < m_tracks.size())
        ? m_tracks.at(m_currentIndex).artist : QString();
}

QString PlaylistModel::currentFileName() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_tracks.size())
        return QString();
    const Track &track = m_tracks.at(m_currentIndex);
    return track.isStream ? track.path : QFileInfo(track.path).fileName();
}

bool PlaylistModel::isCurrentStream() const
{
    return (m_currentIndex >= 0 && m_currentIndex < m_tracks.size())
        && m_tracks.at(m_currentIndex).isStream;
}

QString PlaylistModel::currentPath() const
{
    return (m_currentIndex >= 0 && m_currentIndex < m_tracks.size())
        ? m_tracks.at(m_currentIndex).path : QString();
}

bool PlaylistModel::isSupported(const QString &path)
{
    return kSupportedSuffixes.contains(QFileInfo(path).suffix().toLower());
}

Track PlaylistModel::readMetadata(const QString &path)
{
    Track track;
    track.path = path;

    TagLib::FileRef file(path.toUtf8().constData());
    if (!file.isNull()) {
        if (TagLib::Tag *tag = file.tag()) {
            track.title = QString::fromStdString(tag->title().to8Bit(true)).trimmed();
            track.artist = QString::fromStdString(tag->artist().to8Bit(true)).trimmed();
            track.album = QString::fromStdString(tag->album().to8Bit(true)).trimmed();
        }
        if (TagLib::AudioProperties *audio = file.audioProperties())
            track.durationSec = audio->lengthInSeconds();
    }

    // An untagged file is still a track. Fall back to the filename rather than a blank row.
    if (track.title.isEmpty()) {
        track.title = QFileInfo(path).completeBaseName();
        track.titleFromFilename = true;
    }
    return track;
}

void PlaylistModel::supplyMetadata(int row, const QString &title, const QString &artist)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    Track &track = m_tracks[row];
    QList<int> roles;

    // Trimmed here as well as at the source. A whitespace-only tag is not a title, and letting
    // one through replaced a perfectly good filename with a blank - it passes isEmpty() while
    // displaying as nothing at all.
    const QString cleanTitle = title.trimmed();
    const QString cleanArtist = artist.trimmed();

    // Only a title that was really the filename may be replaced. A file that carried a tag keeps
    // it, which is what "TagLib is authoritative" has meant since streams were added.
    if (track.titleFromFilename && !cleanTitle.isEmpty() && cleanTitle != track.title) {
        track.title = cleanTitle;
        track.titleFromFilename = false;
        roles.append(TitleRole);
    }
    if (track.artist.isEmpty() && !cleanArtist.isEmpty()) {
        track.artist = cleanArtist;
        roles.append(ArtistRole);
    }
    if (roles.isEmpty())
        return;

    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, roles);
    // currentTitle/currentArtist notify on this signal, so the transport picks the new values up.
    if (row == m_currentIndex)
        emit currentIndexChanged();
}

void PlaylistModel::appendPaths(const QStringList &paths)
{
    QList<Track> incoming;
    incoming.reserve(paths.size());
    for (const QString &path : paths) {
        if (isSupported(path))
            incoming.append(readMetadata(path));
    }
    if (incoming.isEmpty())
        return;

    beginInsertRows(QModelIndex(), m_tracks.size(), m_tracks.size() + incoming.size() - 1);
    m_tracks.append(incoming);
    endInsertRows();
    emit countChanged();
    if (m_persist)
        saveSession();
}

void PlaylistModel::addFiles(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls)
        paths.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
    appendPaths(paths);
}

void PlaylistModel::addStream(const QString &url, const QString &name)
{
    const QUrl parsed(url.trimmed());
    if (!parsed.isValid() || parsed.scheme().isEmpty() || parsed.host().isEmpty())
        return;

    // An allow-list rather than "any scheme with a host". playbin3 autoplugs a source element
    // from the URI scheme, so without this a playlist file could name smb://, rtsp:// or
    // anything else GStreamer happens to support and have Reverie contact it. Internet radio is
    // http and https; nothing else has ever been the feature.
    //
    // One rule for typed URLs and for playlist entries alike. Two paths would drift, and the
    // one that drifts is always the one nobody is looking at.
    static const QStringList allowed{QStringLiteral("http"), QStringLiteral("https")};
    if (!allowed.contains(parsed.scheme().toLower())) {
        qWarning("playlist: refused a %s:// stream - only http and https are accepted",
                 qPrintable(parsed.scheme().toLower()));
        return;
    }

    Track track;
    track.path = parsed.toString();
    track.isStream = true;
    track.title = name.trimmed().isEmpty() ? parsed.host() : name.trimmed();

    beginInsertRows(QModelIndex(), m_tracks.size(), m_tracks.size());
    m_tracks.append(track);
    endInsertRows();
    emit countChanged();
    if (m_persist)
        saveSession();
}

void PlaylistModel::addFolder(const QUrl &folder)
{
    const QString root = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    QStringList paths;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        paths.append(it.next());
    paths.sort(Qt::CaseInsensitive);
    appendPaths(paths);
}

void PlaylistModel::removeRowsAt(const QList<int> &rows)
{
    QList<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());

    const QString currentlyPlaying = currentPath();

    for (int row : sorted) {
        if (row < 0 || row >= m_tracks.size())
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_tracks.removeAt(row);
        endRemoveRows();
    }

    // Keep pointing at the same track if it survived, rather than at whatever slid into
    // its row. Removing the playing track clears the selection instead of jumping.
    int restored = -1;
    if (!currentlyPlaying.isEmpty()) {
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks.at(i).path == currentlyPlaying) {
                restored = i;
                break;
            }
        }
    }
    m_currentIndex = restored;
    emit currentIndexChanged();
    emit countChanged();
    if (m_persist)
        saveSession();
}

void PlaylistModel::moveRow(int from, int to)
{
    if (from == to || from < 0 || from >= m_tracks.size() || to < 0 || to >= m_tracks.size())
        return;

    const QString currentlyPlaying = currentPath();
    const int destination = to > from ? to + 1 : to;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
    m_tracks.move(from, to);
    endMoveRows();

    if (!currentlyPlaying.isEmpty()) {
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks.at(i).path == currentlyPlaying) {
                m_currentIndex = i;
                break;
            }
        }
        emit currentIndexChanged();
    }
    if (m_persist)
        saveSession();
}

void PlaylistModel::clear()
{
    if (m_tracks.isEmpty())
        return;
    beginResetModel();
    m_tracks.clear();
    m_currentIndex = -1;
    endResetModel();
    emit currentIndexChanged();
    emit countChanged();
    if (m_persist)
        saveSession();
}

bool PlaylistModel::saveM3U(const QUrl &destination) const
{
    const QString path = destination.isLocalFile() ? destination.toLocalFile()
                                                   : destination.toString();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    const QFileInfo playlistInfo(path);
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "#EXTM3U\n";
    for (const Track &track : m_tracks) {
        const QString artist = track.artist.isEmpty() ? QString() : track.artist + QStringLiteral(" - ");
        out << "#EXTINF:" << (track.isStream ? -1 : track.durationSec) << ','
            << artist << track.title << '\n';

        // A stream is a URL and must be written verbatim; making it relative to the playlist
        // folder would be nonsense.
        if (track.isStream) {
            out << track.path << '\n';
            continue;
        }

        // Relative paths keep a playlist usable when its folder is moved or shared, but only
        // when the track actually lives under that folder. Otherwise relativeFilePath walks
        // up to the filesystem root and emits something like ../../../../../home/... which is
        // both unreadable and fragile. The saved session lands in this second case every time.
        const QString relative = playlistInfo.dir().relativeFilePath(track.path);
        const bool insideTree = !relative.startsWith(QStringLiteral(".."));
        out << (insideTree ? relative : track.path) << '\n';
    }
    return true;
}

bool PlaylistModel::loadM3U(const QUrl &source)
{
    const QString path = source.isLocalFile() ? source.toLocalFile() : source.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    // A playlist is a list of paths. None of these bounds can be reached by a real one, and
    // without them a hostile or corrupt file is read into memory whole - readLine() on a file
    // with no newlines will happily allocate all of it.
    constexpr qint64 kMaxPlaylistBytes = 8 * 1024 * 1024;
    constexpr int kMaxLineChars = 4096;
    constexpr int kMaxEntries = 20000;
    if (file.size() > kMaxPlaylistBytes) {
        qWarning("playlist: %s is %lld bytes, which is not a playlist; refused",
                 qPrintable(QFileInfo(path).fileName()), static_cast<long long>(file.size()));
        return false;
    }

    const QDir base = QFileInfo(path).dir();
    QStringList paths;
    QString pendingTitle;
    int entries = 0;
    bool truncated = false;
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        if (entries >= kMaxEntries) {
            truncated = true;
            break;
        }
        const QString rawLine = in.readLine(kMaxLineChars);
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('#'))) {
            // #EXTINF:<seconds>,<title> - the only place a station name can survive a save.
            if (line.startsWith(QStringLiteral("#EXTINF:"))) {
                const int comma = line.indexOf(QLatin1Char(','));
                pendingTitle = comma >= 0 ? line.mid(comma + 1).trimmed() : QString();
            }
            continue;
        }
        ++entries;
        if (line.contains(QStringLiteral("://"))) {
            addStream(line, pendingTitle);
        } else {
            paths.append(QDir::isAbsolutePath(line) ? line : base.absoluteFilePath(line));
        }
        pendingTitle.clear();
    }
    if (truncated)
        qWarning("playlist: stopped after %d entries", kMaxEntries);
    appendPaths(paths);
    return true;
}

void PlaylistModel::setRepeatMode(RepeatMode mode)
{
    if (mode == m_repeatMode)
        return;
    m_repeatMode = mode;
    QSettings().setValue(QStringLiteral("playlist/repeatMode"), static_cast<int>(mode));
    emit repeatModeChanged();
}

void PlaylistModel::cycleRepeat()
{
    switch (m_repeatMode) {
    case RepeatNone: setRepeatMode(RepeatAll); break;
    case RepeatAll:  setRepeatMode(RepeatOne); break;
    case RepeatOne:  setRepeatMode(RepeatNone); break;
    }
}

void PlaylistModel::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    QSettings().setValue(QStringLiteral("playlist/shuffle"), shuffle);
    emit shuffleChanged();
}

int PlaylistModel::nextForPlayback(bool automatic) const
{
    if (m_tracks.isEmpty())
        return -1;

    // Repeating one track is about the track ending, not about the Next button.
    if (automatic && m_repeatMode == RepeatOne)
        return m_currentIndex >= 0 ? m_currentIndex : 0;

    if (m_shuffle) {
        if (m_tracks.size() == 1)
            return 0;
        // Anything but the track just played. Picking uniformly and retrying once is enough:
        // a true shuffle order would need history the target user has not asked for.
        int candidate = QRandomGenerator::global()->bounded(m_tracks.size());
        if (candidate == m_currentIndex)
            candidate = (candidate + 1) % m_tracks.size();
        return candidate;
    }

    if (m_currentIndex + 1 < m_tracks.size())
        return m_currentIndex + 1;

    // Past the end. Wrapping is what "repeat the playlist" means; pressing Next by hand wraps
    // too, because a dead button at the end of a list reads as broken.
    if (m_repeatMode == RepeatAll || !automatic)
        return 0;
    return -1;
}

int PlaylistModel::nextIndex(bool wrap) const
{
    if (m_tracks.isEmpty())
        return -1;
    if (m_currentIndex + 1 < m_tracks.size())
        return m_currentIndex + 1;
    return wrap ? 0 : -1;
}

int PlaylistModel::previousIndex(bool wrap) const
{
    if (m_tracks.isEmpty())
        return -1;
    if (m_currentIndex - 1 >= 0)
        return m_currentIndex - 1;
    return wrap ? m_tracks.size() - 1 : -1;
}

QString PlaylistModel::sessionFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/session.m3u");
}

void PlaylistModel::saveSession() const
{
    saveM3U(QUrl::fromLocalFile(sessionFilePath()));
}

void PlaylistModel::restoreSession()
{
    const QString path = sessionFilePath();
    if (QFile::exists(path))
        loadM3U(QUrl::fromLocalFile(path));
}
