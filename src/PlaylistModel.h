#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

struct Track
{
    QString path;
    QString title;
    QString artist;
    QString album;
    int durationSec = 0;
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
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentIndexChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationRole,
        DurationTextRole,
        IsCurrentRole,
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
    QString currentPath() const;

public slots:
    void addFiles(const QList<QUrl> &urls);
    void addFolder(const QUrl &folder);
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

private:
    void appendPaths(const QStringList &paths);
    static Track readMetadata(const QString &path);
    static QString sessionFilePath();
    static bool isSupported(const QString &path);

    QList<Track> m_tracks;
    int m_currentIndex = -1;
    bool m_persist = false;
};
