// Qt, but no Qt Quick: a plain QOpenGLWindow driving projectM into an FBO.
// Distinguishes "Qt's GL context" from "Qt Quick's scene graph".
#include <QGuiApplication>
#include <QOpenGLWindow>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTimer>
#include <cmath>
#include <vector>
#include <cstdio>
#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include <projectM-4/logging.h>

class W : public QOpenGLWindow
{
public:
    explicit W(const QString &dir) : m_dir(dir) {}
protected:
    void initializeGL() override
    {
        projectm_set_log_level(PROJECTM_LOG_LEVEL_DEBUG, false);
        projectm_set_log_callback([](const char *m, projectm_log_level l, void *) {
            if (l >= PROJECTM_LOG_LEVEL_WARN) printf("  projectM[%d]: %s\n", l, m);
        }, false, nullptr);
        m_pm = projectm_create();
        if (!m_pm) { printf("  projectm_create failed\n"); return; }
        m_pl = projectm_playlist_create(m_pm);
        projectm_playlist_add_path(m_pl, m_dir.toUtf8().constData(), true, false);
        const uint32_t n = projectm_playlist_size(m_pl);
        projectm_playlist_set_position(m_pl, n / 2, true);
        printf("  playlist %u entries\n", n);
        auto *f = context()->functions();
        printf("  GL: %s | %s\n", f->glGetString(GL_VENDOR), f->glGetString(GL_RENDERER));
    }
    void resizeGL(int w, int h) override
    {
        m_w = w; m_h = h;
        delete m_fbo;
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        m_fbo = new QOpenGLFramebufferObject(QSize(w, h), fmt);
        if (m_pm) projectm_set_window_size(m_pm, w, h);
        printf("  fbo %dx%d\n", w, h);
    }
    void paintGL() override
    {
        if (!m_pm || !m_fbo) return;
        std::vector<float> pcm(512);
        for (int i = 0; i < 512; ++i)
            pcm[i] = 0.35f * sinf((m_frame * 512 + i) * 0.02f) + 0.15f * sinf((m_frame * 512 + i) * 0.11f);
        projectm_pcm_add_float(m_pm, pcm.data(), 512, PROJECTM_MONO);
        auto *f = context()->functions();
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo->handle());
        projectm_opengl_render_frame_fbo(m_pm, m_fbo->handle());
        if (++m_frame % 30 == 0) {
            std::vector<unsigned char> px(m_w * m_h * 4);
            f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo->handle());
            f->glReadPixels(0, 0, m_w, m_h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            long sum = 0; int nb = 0;
            for (int i = 0; i < m_w * m_h; i += 7) {
                int v = px[i*4] + px[i*4+1] + px[i*4+2]; sum += v; if (v > 12) ++nb;
            }
            const int n = (m_w * m_h + 6) / 7;
            printf("  frame %3d: non-black %d/%d (%.0f%%) mean %.1f\n", m_frame, nb, n,
                   100.0 * nb / n, sum / double(n * 3));
            fflush(stdout);
        }
        f->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        update();
    }
private:
    QString m_dir; projectm_handle m_pm = nullptr; projectm_playlist_handle m_pl = nullptr;
    QOpenGLFramebufferObject *m_fbo = nullptr; int m_w = 0, m_h = 0, m_frame = 0;
};

int main(int argc, char **argv)
{
    QSurfaceFormat f;
    f.setVersion(4, 5); f.setProfile(QSurfaceFormat::CoreProfile);
    f.setDepthBufferSize(24); f.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(f);
    QGuiApplication app(argc, argv);
    W w(argc > 1 ? argv[1] : "/usr/share/reverie/presets");
    w.resize(800, 600); w.show();
    QTimer::singleShot((argc > 2 ? atoi(argv[2]) : 15) * 1000, &app, &QGuiApplication::quit);
    return app.exec();
}
