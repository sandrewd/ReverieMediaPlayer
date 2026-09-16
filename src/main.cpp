#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
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
    app.setApplicationName("player-spike");

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
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("initialPreset", parser.value(presetOption));
    engine.rootContext()->setContextProperty("initialScale", parser.value(scaleOption).toDouble());

    // Qt 6.4 puts QML module resources under qrc:/<URI>/; the qrc:/qt/qml/<URI>/ layout
    // only arrives in 6.5. Noble ships 6.4.2, so this path is version-sensitive.
    engine.load(QUrl(QStringLiteral("qrc:/Player/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // The visualizer is judged by its own numbers, never by watching it over a remote
    // desktop stream, so the spike can run unattended and report on exit.
    // Grab what is actually composited. The frame timings say projectM is doing work;
    // they say nothing about whether a single pixel reaches the window.
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
