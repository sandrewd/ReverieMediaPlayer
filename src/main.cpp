#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QFileInfo>
#include <QUrl>

#include "PlaylistModel.h"
#include "AudioEngine.h"
#include "MprisAdaptor.h"

#include <QSurfaceFormat>
#include <QTimer>

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

    QGuiApplication app(argc, argv);
    // QSettings and QStandardPaths both key off these, so the persistence toggle and the
    // saved session live under ~/.config/player and ~/.local/share/player.
    app.setOrganizationName("player");
    app.setApplicationName("player");

    QCommandLineParser parser;
    parser.setApplicationDescription("Phase 0 spike: projectM inside QML's scene graph.");
    parser.addHelpOption();
    QCommandLineOption presetOption({"p", "preset"}, "Preset file to load.", "file");
    QCommandLineOption scaleOption({"s", "scale"}, "Initial render scale (0.1-1.0).", "scale", "0.5");
    QCommandLineOption secondsOption("seconds", "Exit after N seconds (for benchmarking).", "n", "0");
    QCommandLineOption captureOption("capture", "Grab the window to a PNG and exit.", "file");
    parser.addOption(presetOption);
    parser.addOption(scaleOption);
    parser.addOption(secondsOption);
    parser.addOption(captureOption);
    QCommandLineOption autoplayOption("autoplay", "Start playing the first track immediately.");
    parser.addOption(autoplayOption);
    QCommandLineOption miniOption("mini", "Start in mini-player mode.");
    parser.addOption(miniOption);
    QCommandLineOption presetsOption("presets", "Directory holding the preset library.", "dir");
    parser.addOption(presetsOption);
    QCommandLineOption fullscreenOption("fullscreen", "Start with the visualiser fullscreen.");
    parser.addOption(fullscreenOption);
    parser.addPositionalArgument("files", "Audio files or folders to add to the playlist.",
                                 "[files...]");
    parser.process(app);

    // Find the preset library without requiring an install step during development.
    QString presetsDir = parser.isSet(presetsOption) ? parser.value(presetsOption) : QString();
    if (presetsDir.isEmpty()) {
        for (const QString &candidate : {QStringLiteral("assets/presets"),
                                         QCoreApplication::applicationDirPath() + "/assets/presets",
                                         QStringLiteral(PLAYER_SOURCE_DIR "/assets/presets")}) {
            if (QFileInfo::exists(candidate)) {
                presetsDir = candidate;
                break;
            }
        }
    }
    QString curatedList = QStringLiteral(PLAYER_SOURCE_DIR "/assets/presets-curated.txt");
    if (!QFileInfo::exists(curatedList))
        curatedList.clear();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("initialPreset", parser.value(presetOption));
    engine.rootContext()->setContextProperty("initialScale", parser.value(scaleOption).toDouble());
    engine.rootContext()->setContextProperty("initialPresetsPath", presetsDir);
    engine.rootContext()->setContextProperty("initialCuratedList", curatedList);

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
    });
    QObject::connect(mpris, &MprisPlayer::quitRequested, &app, &QGuiApplication::quit);
    mpris->registerService();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        {
            QList<QUrl> files;
            for (const QString &argument : positional) {
                const QFileInfo info(argument);
                if (info.isDir())
                    playlist->addFolder(QUrl::fromLocalFile(info.absoluteFilePath()));
                else
                    files.append(QUrl::fromLocalFile(info.absoluteFilePath()));
            }
            if (!files.isEmpty())
                playlist->addFiles(files);

            if (parser.isSet(autoplayOption) && playlist->rowCount() > 0) {
                QMetaObject::invokeMethod(engine.rootObjects().first(), "playIndex",
                                          Q_ARG(QVariant, 0));
            }
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
