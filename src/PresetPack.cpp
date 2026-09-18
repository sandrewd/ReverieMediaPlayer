#include "PresetPack.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcPack, "reverie.presetpack", QtInfoMsg)

namespace {

// Published as its own release rather than attached to an application version: the collection
// does not change when Reverie does, and pinning a download by digest is only meaningful if the
// digest is stable. Verified against the served asset before being written here.
constexpr auto kPackUrl =
    "https://github.com/sandrewd/ReverieMediaPlayer/releases/download/presets-v1/"
    "reverie-presets-full.tar.xz";
constexpr auto kPackSha256 =
    "4e82a14d1a3fcee32c19bc262f15e293fd1a4bb65637b8f6d34b968b712118ed";
constexpr int kPackCount = 9795;
constexpr qint64 kPackBytes = 2786704;

int countPresets(const QString &dir)
{
    if (dir.isEmpty() || !QFileInfo::exists(dir))
        return 0;
    int n = 0;
    QDirIterator it(dir, {QStringLiteral("*.milk")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

} // namespace

PresetPack::PresetPack(QObject *parent)
    : QObject(parent)
{
    m_useFullPack = QSettings().value(QStringLiteral("visualisation/useFullPack"), false).toBool();
    // A preference stored while the pack existed must not survive the pack being removed, or the
    // visualiser is pointed at a directory that is no longer there and shows nothing.
    if (m_useFullPack && !installed())
        m_useFullPack = false;
}

PresetPack::~PresetPack()
{
    cancel();
}

QString PresetPack::installRoot()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/reverie/presets-full");
}

bool PresetPack::installed() const
{
    // A directory alone is not enough: an interrupted extraction can leave one behind. The
    // presence of actual presets is the only thing worth trusting.
    if (m_cachedCount < 0)
        m_cachedCount = countPresets(installRoot());
    return m_cachedCount > 0;
}

int PresetPack::installedCount() const
{
    if (m_cachedCount < 0)
        m_cachedCount = countPresets(installRoot());
    return m_cachedCount;
}

QString PresetPack::downloadSize() const
{
    return QStringLiteral("%1 MB").arg(kPackBytes / 1048576.0, 0, 'f', 1);
}

int PresetPack::packCount() const
{
    return kPackCount;
}

QString PresetPack::activePresetsPath() const
{
    if (m_useFullPack && installed())
        return installRoot();
    return m_bundledPath;
}

void PresetPack::setBundledPresetsPath(const QString &path)
{
    if (m_bundledPath == path)
        return;
    m_bundledPath = path;
    emit activePresetsPathChanged();
}

void PresetPack::setUseFullPack(bool use)
{
    if (use && !installed())
        use = false;
    const bool changed = (m_useFullPack != use);
    m_useFullPack = use;
    if (changed) {
        QSettings().setValue(QStringLiteral("visualisation/useFullPack"), use);
        emit activePresetsPathChanged();
    }
    // Emitted even when nothing changed, deliberately. QML detaches a MenuItem's `checked`
    // binding the instant it assigns on click, and only re-attaches when the bound property
    // signals - so an exclusive item whose handler is effectively a no-op unchecks itself and
    // leaves the group empty. This project has hit that three times; see the brief.
    emit useFullPackChanged();
}

void PresetPack::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void PresetPack::setMessage(const QString &m)
{
    if (m_message == m)
        return;
    m_message = m;
    emit messageChanged();
}

void PresetPack::setProgress(qreal p)
{
    if (qFuzzyCompare(m_progress, p))
        return;
    m_progress = p;
    emit progressChanged();
}

void PresetPack::fail(const QString &why)
{
    qCWarning(lcPack, "preset pack: %s", qPrintable(why));
    cleanupTemporary();
    setMessage(why);
    setState(Failed);
    emit finished(false);
}

void PresetPack::cleanupTemporary()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_tar) {
        m_tar->disconnect(this);
        if (m_tar->state() != QProcess::NotRunning) {
            m_tar->kill();
            m_tar->waitForFinished(2000);
        }
        m_tar->deleteLater();
        m_tar = nullptr;
    }
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    if (!m_archivePath.isEmpty()) {
        QFile::remove(m_archivePath);
        m_archivePath.clear();
    }
    if (!m_stagingPath.isEmpty()) {
        QDir(m_stagingPath).removeRecursively();
        m_stagingPath.clear();
    }
}

void PresetPack::install()
{
    if (m_state == Downloading || m_state == Extracting || m_state == Verifying)
        return;

    cleanupTemporary();
    setProgress(0.0);
    setMessage(tr("Starting…"));
    setState(Downloading);

    const QString cache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    m_archivePath = cache + QStringLiteral("/reverie-presets-full.tar.xz");

    m_file = new QFile(m_archivePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        fail(tr("Could not write to %1.").arg(cache));
        return;
    }

    if (!m_net)
        m_net = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(QString::fromLatin1(kPackUrl))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_net->get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_file)
            m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) {
                const qint64 expected = total > 0 ? total : kPackBytes;
                setProgress(expected > 0 ? qreal(got) / qreal(expected) : 0.0);
                setMessage(tr("Downloading… %1 of %2 MB")
                               .arg(got / 1048576.0, 0, 'f', 1)
                               .arg(expected / 1048576.0, 0, 'f', 1));
            });
    connect(m_reply, &QNetworkReply::finished, this, &PresetPack::onDownloadFinished);
}

void PresetPack::onDownloadFinished()
{
    if (!m_reply)
        return;

    const auto error = m_reply->error();
    if (m_file)
        m_file->write(m_reply->readAll());

    m_reply->deleteLater();
    m_reply = nullptr;

    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }

    if (error == QNetworkReply::OperationCanceledError) {
        cleanupTemporary();
        setMessage(QString());
        setState(Idle);
        emit finished(false);
        return;
    }
    if (error != QNetworkReply::NoError) {
        fail(tr("Download failed. Check your connection and try again."));
        return;
    }

    setState(Verifying);
    setMessage(tr("Checking the download…"));

    QFile f(m_archivePath);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(tr("Could not read the downloaded file."));
        return;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        fail(tr("Could not read the downloaded file."));
        return;
    }
    const QString got = QString::fromLatin1(hash.result().toHex());
    if (got != QString::fromLatin1(kPackSha256)) {
        // Not pedantry: this is the only thing standing between a corrupted or substituted
        // download and 9,795 files written into the user's home directory.
        fail(tr("The download did not match its checksum and was discarded."));
        return;
    }

    extract();
}

void PresetPack::extract()
{
    setState(Extracting);
    setMessage(tr("Installing…"));
    setProgress(1.0);

    // Extract into a staging directory and rename it into place, so an interrupted run cannot
    // leave a half-populated library that looks installed.
    m_stagingPath = installRoot() + QStringLiteral(".part");
    QDir(m_stagingPath).removeRecursively();
    if (!QDir().mkpath(m_stagingPath)) {
        fail(tr("Could not create %1.").arg(m_stagingPath));
        return;
    }

    m_tar = new QProcess(this);
    connect(m_tar, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { onExtractFinished(code); });
    connect(m_tar, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            // A .deb always has tar, and the Flatpak runtime carries tar and xz - both checked.
            // A snap contains only what it stages, so this is the message that will identify a
            // snapcraft.yaml missing tar and xz-utils.
            fail(tr("Could not run tar, which is needed to unpack the presets."));
        }
    });
    // Program plus argument list, never a shell: no part of this is user-supplied, and it stays
    // that way by construction.
    m_tar->start(QStringLiteral("tar"),
                 {QStringLiteral("-xJf"), m_archivePath, QStringLiteral("-C"), m_stagingPath});
}

void PresetPack::onExtractFinished(int exitCode)
{
    if (exitCode != 0) {
        fail(tr("Could not unpack the presets."));
        return;
    }

    const int staged = countPresets(m_stagingPath);
    if (staged <= 0) {
        fail(tr("The downloaded pack contained no presets."));
        return;
    }

    const QString target = installRoot();
    QDir(target).removeRecursively();
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QDir().rename(m_stagingPath, target)) {
        fail(tr("Could not move the presets into place."));
        return;
    }
    m_stagingPath.clear();

    m_cachedCount = staged;
    qCInfo(lcPack, "preset pack: installed %d presets to %s", staged, qPrintable(target));

    cleanupTemporary();
    setState(Idle);
    setMessage(tr("Installed %1 visualisations.").arg(staged));
    emit installedChanged();
    // Downloading it is the request to use it; leaving the user to find a second switch
    // afterwards would make the download look like it had done nothing.
    setUseFullPack(true);
    emit finished(true);
}

void PresetPack::cancel()
{
    if (m_state == Idle || m_state == Failed)
        return;
    cleanupTemporary();
    setProgress(0.0);
    setMessage(QString());
    setState(Idle);
    emit finished(false);
}

void PresetPack::remove()
{
    cancel();
    setUseFullPack(false);
    QDir(installRoot()).removeRecursively();
    m_cachedCount = 0;
    qCInfo(lcPack, "preset pack: removed");
    setMessage(QString());
    emit installedChanged();
    emit activePresetsPathChanged();
}
