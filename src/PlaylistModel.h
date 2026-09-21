#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

struct Track
{
    QString path;       // a local path, or a URL for a stream
    QString title;
    QString artist;
    QString album;
    int durationSec = 0;
    bool isStream = false;
    // True when `title` is really the filename, because the file carried no tag TagLib could
    // read. Only such a title may be replaced by what the decoder reports later.
    bool titleFromFilename = false;
};

// One playlist. Not tabbed playlists, not multiple queues - that was decided and it keeps
// the model, the view and the persistence story simple enough for the target user.
class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool persistAcrossLaunches READ persistAcrossLaunches
                   WRITE setPersistAcrossLaunches NOTIFY persistAcrossLaunchesChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY currentIndexChanged)
    // What the transport shows on hover: the thing the tags were derived from.
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY currentIndexChanged)
    Q_PROPERTY(RepeatMode repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentIndexChanged)
    Q_PROPERTY(bool currentIsStream READ isCurrentStream NOTIFY currentIndexChanged)

public:
    enum RepeatMode {
        RepeatNone = 0,   // stop at the end of the list
        RepeatAll,        // wrap around
        RepeatOne         // replay the current track
    };
    Q_ENUM(RepeatMode)

    RepeatMode repeatMode() const { return m_repeatMode; }
    void setRepeatMode(RepeatMode mode);
    Q_INVOKABLE void cycleRepeat();
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);

    // What to play when the current track ends, or when Next is pressed. `automatic` separates
    // the two: repeating one track means the *end of the track* replays it, while pressing Next
    // still moves on - anything else makes the button look broken.
    Q_INVOKABLE int nextForPlayback(bool automatic) const;

    // Tags the decoder found while playing, offered to a row that has none. Deliberately a
    // *fill*, never an override: TagLib stays authoritative for a file that actually carries
    // tags, and only a title that was really the filename may be replaced.
    Q_INVOKABLE void supplyMetadata(int row, const QString &title, const QString &artist);

    enum Roles {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationRole,
        DurationTextRole,
        IsCurrentRole,
        IsStreamRole,
        FileNameRole,
    };

    explicit PlaylistModel(QObject *parent = nullptr);
    ~PlaylistModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    bool persistAcrossLaunches() const { return m_persist; }
    void setPersistAcrossLaunches(bool persist);

    QString currentTitle() const;
    QString currentArtist() const;
    QString currentFileName() const;
    QString currentPath() const;

public slots:
    void addFiles(const QList<QUrl> &urls);
    // Streams carry no tags to read, so the name is whatever the user calls the station;
    // anything the station itself announces arrives later as a GStreamer tag.
    void addStream(const QString &url, const QString &name = QString());
    bool isCurrentStream() const;
    void addFolder(const QUrl &folder);

    // What a drag-and-drop actually delivers. A dropped folder and a dropped file are the same
    // kind of URL, and whether a directory carries a trailing slash is up to the file manager -
    // Dolphin does not add one, which made folders fall into the file path, get filtered out by
    // extension, and vanish without a word. Ask the filesystem rather than reading the string.
    Q_INVOKABLE void addDropped(const QList<QUrl> &urls);
    void removeRowsAt(const QList<int> &rows);
    void moveRow(int from, int to);
    void clear();
    bool saveM3U(const QUrl &destination) const;
    bool loadM3U(const QUrl &source);
    int nextIndex(bool wrap = true) const;
    int previousIndex(bool wrap = true) const;
    void restoreSession();
    void saveSession() const;

signals:
    void currentIndexChanged();
    void countChanged();
    void persistAcrossLaunchesChanged();
    void repeatModeChanged();
    void shuffleChanged();

private:
    void appendPaths(const QStringList &paths);
    static Track readMetadata(const QString &path);
    static QString sessionFilePath();

    RepeatMode m_repeatMode = RepeatNone;
    bool m_shuffle = false;
    static bool isSupported(const QString &path);

    QList<Track> m_tracks;
    int m_currentIndex = -1;
    bool m_persist = false;
};
