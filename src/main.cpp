#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>

#include <gst/gst.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include "PlaylistModel.h"
#include "AudioEngine.h"
#include "MprisAdaptor.h"

#include <QSurfaceFormat>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QWindow>

namespace {

// The application used to be called "player". Copy the old settings and saved session across
// the first time we run under the new name, and leave the originals alone: if this turns out
// to be wrong, nothing has been destroyed.
void migrateLegacySettings()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString legacyConfig = configRoot + QStringLiteral("/player/player.conf");
    const QString currentConfig = configRoot + QStringLiteral("/reverie/reverie.conf");
    if (QFileInfo::exists(legacyConfig) && !QFileInfo::exists(currentConfig)) {
        QDir().mkpath(QFileInfo(currentConfig).absolutePath());
        if (QFile::copy(legacyConfig, currentConfig))
            qInfo("migrated settings from the previous application name");
    }

    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString legacySession = dataRoot + QStringLiteral("/player/player/session.m3u");
    const QString currentSession = dataRoot + QStringLiteral("/reverie/reverie/session.m3u");
    if (QFileInfo::exists(legacySession) && !QFileInfo::exists(currentSession)) {
        QDir().mkpath(QFileInfo(currentSession).absolutePath());
        QFile::copy(legacySession, currentSession);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // projectM speaks raw OpenGL and requires every call on the thread owning its context.
    // Qt 6 renders through RHI and would otherwise be free to pick a different backend, so
    // pin it to OpenGL before QGuiApplication reads the environment.
    qputenv("QSG_RHI_BACKEND", "opengl");

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    // GStreamer up before the QML engine, so element creation during QML load is safe.
    gst_init(&argc, &argv);

    QGuiApplication app(argc, argv);
    // QSettings and QStandardPaths key off these. applicationName stays lowercase and
    // space-free because it becomes a directory and a filename; the display name is separate.
    app.setOrganizationName("reverie");
    app.setApplicationName("reverie");
    app.setApplicationDisplayName("Reverie Media Player");
    app.setApplicationVersion(PLAYER_VERSION);

    // The launcher icon is fixed colour and never themed: a .desktop icon is drawn by the
    // shell with no access to our palette, and an icon that tracked the user's colours would
    // make two installs look like different applications. Identity stays constant; the
    // interface is theirs.
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/branding/%1x%1/apps/reverie.png").arg(size),
                     QSize(size, size));
    }
    if (!icon.isNull())
        app.setWindowIcon(icon);

    // Carry settings over from the pre-rename identity once, so saved palettes and the
    // playlist preference survive the change of name.
    migrateLegacySettings();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Reverie Media Player — a media player with the visualisations built in.");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption presetOption({"p", "preset"}, "Preset file to load.", "file");
    // Default of 0 means "not specified": the stored preference wins unless overridden.
    QCommandLineOption scaleOption({"s", "scale"}, "Override the stored render scale (0.1-1.0).",
                                   "scale", "0");
    QCommandLineOption secondsOption("seconds", "Exit after N seconds (for benchmarking).", "n", "0");
    QCommandLineOption captureOption("capture", "Grab the window to a PNG and exit.", "file");
    parser.addOption(presetOption);
    parser.addOption(scaleOption);
    parser.addOption(secondsOption);
    parser.addOption(captureOption);
    QCommandLineOption autoplayOption("autoplay",
        "Start playing a restored playlist. Files given as arguments already play.");
    parser.addOption(autoplayOption);
    QCommandLineOption miniOption("mini", "Start in mini-player mode.");
    parser.addOption(miniOption);
    QCommandLineOption presetsOption("presets", "Directory holding the preset library.", "dir");
    parser.addOption(presetsOption);
    QCommandLineOption fullscreenOption("fullscreen", "Start with the visualiser fullscreen.");
    parser.addOption(fullscreenOption);
    QCommandLineOption fpsCapOption("max-fps", "Visualiser frame cap; 0 is uncapped.", "n", "60");
    parser.addOption(fpsCapOption);
    parser.addPositionalArgument("files", "Audio files or folders to add to the playlist.",
                                 "[files...]");
    parser.process(app);

    // Single instance. Opening a second file from a file manager used to start a second Reverie,
    // which played over the first and lost the MPRIS name, so media keys kept controlling the
    // window you were not looking at. If somebody already owns the name, hand them what we were
    // given and get out of the way.
    //
    // Deliberately before anything expensive: no QML engine, no projectM, no preset scan. The
    // second process should cost almost nothing and disappear.
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (bus.isConnected() && bus.interface()
            && bus.interface()->isServiceRegistered(QString::fromLatin1(MprisPlayer::serviceName()))) {
            // An older Reverie owns the name but has none of this interface, so the hand-off
            // silently fails - and exiting anyway means the upgrade appears not to have taken:
            // the user keeps interacting with the old window and every relaunch quits on sight.
            // Check first, and if nobody answers, carry on and open our own window.
            QDBusInterface probe(QString::fromLatin1(MprisPlayer::serviceName()),
                                 QString::fromLatin1(MprisPlayer::objectPath()),
                                 QString::fromLatin1(MprisPlayer::appInterface()), bus);
            const QDBusMessage reply =
                probe.call(QDBus::BlockWithGui, QStringLiteral("Activate"), QString());
            if (reply.type() == QDBusMessage::ErrorMessage) {
                qInfo("%s is owned by something that does not speak our interface (%s); "
                      "starting a window of our own",
                      MprisPlayer::serviceName(), qPrintable(reply.errorName()));
            } else {

            const QStringList handoff = parser.positionalArguments();
            if (!handoff.isEmpty()) {
                QStringList uris;
                for (const QString &argument : handoff) {
                    uris.append(argument.contains(QStringLiteral("://"))
                                    ? argument
                                    : QUrl::fromLocalFile(
                                          QFileInfo(argument).absoluteFilePath()).toString());
                }
                QDBusInterface reverie(QString::fromLatin1(MprisPlayer::serviceName()),
                                       QString::fromLatin1(MprisPlayer::objectPath()),
                                       QString::fromLatin1(MprisPlayer::appInterface()), bus);
                reverie.call(QStringLiteral("OpenFiles"), uris);
            }
            // Bring the existing window forward, so launching the application twice looks like
            // focusing it rather than doing nothing. The desktop gives the process it launches an
            // activation token; handing that over is what lets the running instance raise itself
            // on Wayland, where a client without one is ignored. X11's equivalent is
            // DESKTOP_STARTUP_ID.
            QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
            if (token.isEmpty())
                token = qEnvironmentVariable("DESKTOP_STARTUP_ID");
            probe.call(QStringLiteral("Activate"), token);
            qInfo("another instance owns %s; handed it %lld file(s) and exiting",
                  MprisPlayer::serviceName(), static_cast<long long>(handoff.size()));
            return 0;
            }
        }
    }

    // Find the preset library. The order matters and is not arbitrary: an installed copy has to
    // win on a user's machine, and the source tree has to win during development, but neither may
    // be assumed to exist. `PLAYER_SOURCE_DIR` is a developer convenience compiled into the
    // binary and is meaningless on anyone else's machine - resolving through it alone is why an
    // installed build found no presets at all.
    //
    // QStandardPaths::GenericDataLocation searches XDG_DATA_HOME and every XDG_DATA_DIRS entry,
    // which covers ~/.local/share, /usr/share and - importantly for packaging - the /app/share a
    // Flatpak runtime puts on that path. So one lookup handles every install layout we care about
    // without the binary needing to know which one it is in.
    const auto findResource = [](const QString &relative) -> QString {
        const QString shared = QStandardPaths::locate(
            QStandardPaths::GenericDataLocation, QStringLiteral("reverie/") + relative,
            relative.endsWith(QStringLiteral(".txt")) ? QStandardPaths::LocateFile
                                                      : QStandardPaths::LocateDirectory);
        if (!shared.isEmpty())
            return shared;
        // Relocatable: a prefix moved wholesale still resolves relative to the executable.
        for (const QString &candidate : {
                 QCoreApplication::applicationDirPath() + QStringLiteral("/../share/reverie/") + relative,
                 QCoreApplication::applicationDirPath() + QStringLiteral("/assets/") + relative,
                 QStringLiteral("assets/") + relative}) {
            if (QFileInfo::exists(candidate))
                return QFileInfo(candidate).absoluteFilePath();
        }
        // Only present in a build configured with -DPLAYER_EMBED_SOURCE_DIR=ON. A released
        // binary carries an empty string here rather than the build machine's home directory.
        if (qstrlen(PLAYER_SOURCE_DIR) > 0) {
            const QString candidate = QStringLiteral(PLAYER_SOURCE_DIR "/assets/") + relative;
            if (QFileInfo::exists(candidate))
                return QFileInfo(candidate).absoluteFilePath();
        }
        return QString();
    };

    QString presetsDir = parser.isSet(presetsOption) ? parser.value(presetsOption) : QString();
    if (presetsDir.isEmpty())
        presetsDir = findResource(QStringLiteral("presets"));
    if (presetsDir.isEmpty())
        qWarning("no preset library found; the visualiser will have nothing to show");
    else
        qInfo("preset library: %s", qPrintable(presetsDir));

    // Only meaningful when the library is larger than the curated selection. A package that ships
    // the curated set alone installs no list, and the menu item that switches between them hides
    // itself rather than appearing to do nothing.
    const QString curatedList = findResource(QStringLiteral("presets-curated.txt"));

    QQmlApplicationEngine engine;

    // The QML module is compiled into the binary. Qt 6.4's default resource prefix is "/", not
    // the "qrc:/qt/qml" that later versions put on the import path automatically, so without
    // this the embedded module is simply not found.
    //
    // This mattered more than it looks. Qt also puts the executable's own directory on the
    // import path, and the build tree contains a generated Player/ module directory — so every
    // run from build/ silently resolved the module from disk and the embedded copy was never
    // exercised. Move the binary anywhere else and the application hung on startup. It only
    // surfaced when it was installed and launched from the applications menu.
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.rootContext()->setContextProperty("initialPreset", parser.value(presetOption));
    engine.rootContext()->setContextProperty("initialScale", parser.value(scaleOption).toDouble());
    engine.rootContext()->setContextProperty("initialPresetsPath", presetsDir);
    engine.rootContext()->setContextProperty("initialCuratedList", curatedList);
    engine.rootContext()->setContextProperty("initialMaxFps", parser.value(fpsCapOption).toInt());

    // Qt 6.4 puts QML module resources under qrc:/<URI>/; the qrc:/qt/qml/<URI>/ layout
    // only arrives in 6.5. Noble ships 6.4.2, so this path is version-sensitive.
    engine.load(QUrl(QStringLiteral("qrc:/Player/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // The visualizer is judged by its own numbers, never by watching it over a remote
    // desktop stream, so the spike can run unattended and report on exit.
    // Grab what is actually composited. The frame timings say projectM is doing work;
    // they say nothing about whether a single pixel reaches the window.
    // Files named on the command line go straight into the one playlist. This is also how
    // a file manager's "Open with" reaches us.
    auto *playlist = engine.singletonInstance<PlaylistModel *>(
        qmlTypeId("Player", 1, 0, "PlaylistModel"));
    auto *audio = engine.singletonInstance<AudioEngine *>(
        qmlTypeId("Player", 1, 0, "AudioEngine"));
    QObject *window = engine.rootObjects().first();

    // Media keys arrive as MPRIS method calls, so next/previous have to go through the same
    // QML entry points the buttons use rather than poking the engine directly - otherwise the
    // playlist's idea of the current track drifts from what is actually playing.
    auto *mpris = new MprisPlayer(audio, playlist, &app);
    QObject::connect(mpris, &MprisPlayer::nextRequested, window,
                     [window]() { QMetaObject::invokeMethod(window, "playNext"); });
    QObject::connect(mpris, &MprisPlayer::previousRequested, window,
                     [window]() { QMetaObject::invokeMethod(window, "playPrevious"); });
    QObject::connect(mpris, &MprisPlayer::raiseRequested, window, [window]() {
        QMetaObject::invokeMethod(window, "show");
        QMetaObject::invokeMethod(window, "raise");
        if (auto *w = qobject_cast<QWindow *>(window))
            w->requestActivate();
    });
    // A relaunch: the process that was started handed us its activation token before exiting.
    // Qt's Wayland plugin reads the token from the environment when a window asks to be
    // activated, so putting it there is what turns requestActivate() from a request the
    // compositor may ignore into one it will honour. Harmless on X11, where raise() suffices.
    QObject::connect(mpris, &MprisPlayer::activateRequested, window,
                     [window](const QString &token) {
        if (!token.isEmpty())
            qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
        QMetaObject::invokeMethod(window, "show");
        QMetaObject::invokeMethod(window, "raise");
        if (auto *w = qobject_cast<QWindow *>(window))
            w->requestActivate();
    });
    QObject::connect(mpris, &MprisPlayer::quitRequested, &app, &QGuiApplication::quit);
    mpris->registerService();

    // Two callers need this: the command line at startup, and a second process handing us its
    // arguments over D-Bus. Sharing one implementation is the point - they behaved differently
    // before and that is how the wrong track came to play.
    auto addAndPlay = [&](const QStringList &arguments, bool replaceExisting) {
        if (arguments.isEmpty())
            return;
        if (replaceExisting)
            playlist->clear();
        // Where the newly-given tracks will land. With playlist persistence on, the list is
        // already populated from the last session, so "play index 0" would start whatever was
        // restored rather than the file the user just handed us.
        const int firstNewRow = playlist->rowCount();
        QList<QUrl> files;
        for (const QString &argument : arguments) {
            // A URL is a stream - except file://, which a second instance uses to forward local
            // paths and which must not be mistaken for one.
            const bool isFileUrl = argument.startsWith(QStringLiteral("file://"));
            if (!isFileUrl && argument.contains(QStringLiteral("://"))) {
                playlist->addStream(argument);
                continue;
            }
            const QFileInfo info(isFileUrl ? QUrl(argument).toLocalFile() : argument);
            if (info.isDir())
                playlist->addFolder(QUrl::fromLocalFile(info.absoluteFilePath()));
            else
                files.append(QUrl::fromLocalFile(info.absoluteFilePath()));
        }
        if (!files.isEmpty())
            playlist->addFiles(files);

        // Being handed a file is a request to play it. This is what "Open with" does from a file
        // manager, and a player that opens a file and then sits there waiting to be told to play
        // it is simply broken - there is no other reason to have opened it.
        if (playlist->rowCount() > firstNewRow) {
            QMetaObject::invokeMethod(engine.rootObjects().first(), "playIndex",
                                      Q_ARG(QVariant, firstNewRow));
        }
    };

    // A second process handed us its files: replace the playlist and play, then come forward.
    QObject::connect(mpris, &MprisPlayer::openFilesRequested, &app,
                     [&addAndPlay, mpris](const QStringList &uris) {
        addAndPlay(uris, true);
        emit mpris->raiseRequested();
    });

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        addAndPlay(positional, false);
    } else if (parser.isSet(autoplayOption) && playlist->rowCount() > 0) {
        // No files given, so this is a restored playlist. Starting it is opt-in: launching the
        // application from the desktop should not begin playing on its own.
        {
            QMetaObject::invokeMethod(engine.rootObjects().first(), "playIndex",
                                      Q_ARG(QVariant, 0));
        }
    }

    if (parser.isSet(miniOption))
        window->setProperty("mini", true);
    if (parser.isSet(fullscreenOption))
        window->setProperty("fullscreen", true);

    const QString capturePath = parser.value(captureOption);
    if (!capturePath.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(4000, &app, [window, capturePath, &app]() {
            if (!window) {
                qWarning("capture: root object is not a QQuickWindow");
                app.exit(2);
                return;
            }
            const QImage shot = window->grabWindow();
            if (shot.isNull() || !shot.save(capturePath))
                qWarning("capture: failed to grab or save");
            else
                qInfo("capture: wrote %s (%dx%d)", qPrintable(capturePath),
                      shot.width(), shot.height());
            app.quit();
        });
    }

    const int seconds = parser.value(secondsOption).toInt();
    if (seconds > 0)
        QTimer::singleShot(seconds * 1000, &app, &QGuiApplication::quit);

    return app.exec();
}
