#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QFile;

// Downloads and installs the full Milkdrop preset collection.
//
// Reverie's packages ship a curated 480 because 9,795 presets unpack to about 132 MB, which is
// not a reasonable size for a media player package. Compressed, though, the whole collection is
// 2.7 MB - presets are plain text and compress roughly fifty to one - so as a *download* it is
// trivial, and measured indexing of 9,795 against 480 costs about 60 ms at startup, which is
// inside the run-to-run noise.
//
// The pack installs alongside the packaged set rather than over it. findResource() searches
// XDG_DATA_HOME before /usr/share, so unpacking into reverie/presets would silently shadow the
// packaged library with no way back short of deleting files by hand.
class PresetPack : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(int installedCount READ installedCount NOTIFY installedChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    // Which library the visualiser should use. Persisted, and forced back to false whenever the
    // pack is absent, so a stored preference cannot leave the visualiser pointed at nothing.
    Q_PROPERTY(bool useFullPack READ useFullPack WRITE setUseFullPack NOTIFY useFullPackChanged)
    // What QML binds the library and the visualiser to.
    Q_PROPERTY(QString activePresetsPath READ activePresetsPath NOTIFY activePresetsPathChanged)
    Q_PROPERTY(QString bundledPresetsPath READ bundledPresetsPath WRITE setBundledPresetsPath
                   NOTIFY activePresetsPathChanged)
    Q_PROPERTY(QString downloadSize READ downloadSize CONSTANT)
    Q_PROPERTY(int packCount READ packCount CONSTANT)

public:
    enum State { Idle, Downloading, Verifying, Extracting, Failed };
    Q_ENUM(State)

    explicit PresetPack(QObject *parent = nullptr);
    ~PresetPack() override;

    State state() const { return m_state; }
    bool installed() const;
    int installedCount() const;
    qreal progress() const { return m_progress; }
    QString message() const { return m_message; }

    bool useFullPack() const { return m_useFullPack; }
    void setUseFullPack(bool use);

    QString activePresetsPath() const;
    QString bundledPresetsPath() const { return m_bundledPath; }
    void setBundledPresetsPath(const QString &path);

    QString downloadSize() const;
    int packCount() const;

    // Where the pack lives once installed: <XDG_DATA_HOME>/reverie/presets-full.
    static QString installRoot();

public slots:
    void install();
    void cancel();
    void remove();

signals:
    void stateChanged();
    void installedChanged();
    void progressChanged();
    void messageChanged();
    void useFullPackChanged();
    void activePresetsPathChanged();
    void finished(bool ok);

private:
    void setState(State s);
    void setMessage(const QString &m);
    void setProgress(qreal p);
    void onDownloadFinished();
    void extract();
    void onExtractFinished(int exitCode);
    void fail(const QString &why);
    void cleanupTemporary();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QProcess *m_tar = nullptr;
    QFile *m_file = nullptr;
    QString m_archivePath;
    QString m_stagingPath;
    QString m_bundledPath;
    QString m_message;
    State m_state = Idle;
    qreal m_progress = 0.0;
    bool m_useFullPack = false;
    mutable int m_cachedCount = -1;
};
