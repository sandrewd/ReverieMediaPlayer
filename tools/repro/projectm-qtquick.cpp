// Minimal Qt Quick + projectM: QQuickFramebufferObject and nothing else of Reverie's.
// Answers whether the fault is in the Qt Quick integration generally or in our application.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickFramebufferObject>
#include <QQuickWindow>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTimer>
#include <QQmlContext>
#include <cmath>
#include <vector>
#include <cstdio>

#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include <projectM-4/logging.h>

static QString gPresetDir;

class PmItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    PmItem() { setMirrorVertically(true); setTextureFollowsItemSize(true); }
    Renderer *createRenderer() const override;
};

class PmRenderer : public QQuickFramebufferObject::Renderer
{
public:
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        m_size = size;
        if (!m_pm) {
            projectm_set_log_level(PROJECTM_LOG_LEVEL_DEBUG, false);
            projectm_set_log_callback([](const char *m, projectm_log_level l, void *) {
                if (l >= PROJECTM_LOG_LEVEL_WARN) printf("  projectM[%d]: %s\n", l, m);
            }, false, nullptr);
            m_pm = projectm_create();
            if (!m_pm) { printf("  projectm_create failed\n"); }
            else {
                m_pl = projectm_playlist_create(m_pm);
                projectm_playlist_add_path(m_pl, gPresetDir.toUtf8().constData(), true, false);
                const uint32_t n = projectm_playlist_size(m_pl);
                projectm_playlist_set_position(m_pl, n / 2, true);
                char *it = n ? projectm_playlist_item(m_pl, n / 2) : nullptr;
                printf("  playlist %u entries, showing %s\n", n, it ? it : "(none)");
                if (it) projectm_free_string(it);
            }
        }
        if (m_pm) projectm_set_window_size(m_pm, size.width(), size.height());
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        printf("  createFramebufferObject %dx%d\n", size.width(), size.height());
        return new QOpenGLFramebufferObject(size, f);
    }

    void render() override
    {
        if (!m_pm) return;
        std::vector<float> pcm(512);
        for (int i = 0; i < 512; ++i)
            pcm[i] = 0.35f * sinf((m_frame * 512 + i) * 0.02f) + 0.15f * sinf((m_frame * 512 + i) * 0.11f);
        projectm_pcm_add_float(m_pm, pcm.data(), 512, PROJECTM_MONO);

        QQuickWindow *w = m_window;
        if (w) w->beginExternalCommands();
        if (auto *t = framebufferObject())
            projectm_opengl_render_frame_fbo(m_pm, t->handle());
        if (w) w->endExternalCommands();

        if (++m_frame % 30 == 0) {
            auto *fns = QOpenGLContext::currentContext()->functions();
            const int W = m_size.width(), H = m_size.height();
            std::vector<unsigned char> px(W * H * 4);
            fns->glBindFramebuffer(GL_FRAMEBUFFER, framebufferObject()->handle());
            fns->glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            long sum = 0; int nb = 0;
            for (int i = 0; i < W * H; i += 7) {
                int v = px[i*4] + px[i*4+1] + px[i*4+2];
                sum += v; if (v > 12) ++nb;
            }
            const int n = (W * H + 6) / 7;
            printf("  frame %3d: non-black %d/%d (%.0f%%) mean %.1f\n",
                   m_frame, nb, n, 100.0 * nb / n, sum / double(n * 3));
            fflush(stdout);
        }
        update();
    }

    void synchronize(QQuickFramebufferObject *item) override { m_window = item->window(); }

private:
    projectm_handle m_pm = nullptr;
    projectm_playlist_handle m_pl = nullptr;
    QQuickWindow *m_window = nullptr;
    QSize m_size;
    int m_frame = 0;
};

QQuickFramebufferObject::Renderer *PmItem::createRenderer() const { return new PmRenderer; }

#include "qmltest.moc"

int main(int argc, char **argv)
{
    qputenv("QSG_RHI_BACKEND", "opengl");
    QSurfaceFormat f;
    f.setVersion(4, 5);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24);
    f.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(f);

    QGuiApplication app(argc, argv);
    gPresetDir = argc > 1 ? argv[1] : QStringLiteral("/usr/share/reverie/presets");
    qmlRegisterType<PmItem>("Pm", 1, 0, "PmItem");

    QQmlApplicationEngine engine;
    engine.loadData(R"(
import QtQuick
import QtQuick.Window
import Pm
Window {
    visible: true; width: 800; height: 600; color: "black"
    PmItem { anchors.fill: parent }
}
)");
    if (engine.rootObjects().isEmpty()) return 1;
    QTimer::singleShot(argc > 2 ? atoi(argv[2]) * 1000 : 20000, &app, &QGuiApplication::quit);
    return app.exec();
}
