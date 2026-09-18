// Standalone projectM exerciser: GLX only, no Qt, no scene graph.
// Answers one question - does projectM render anything on this driver?
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>

extern "C" {
typedef void *projectm_handle;
projectm_handle projectm_create(void);
void projectm_destroy(projectm_handle);
void projectm_set_window_size(projectm_handle, size_t, size_t);
void projectm_opengl_render_frame(projectm_handle);
void projectm_opengl_render_frame_fbo(projectm_handle, uint32_t);
void projectm_load_preset_file(projectm_handle, const char *, bool);
void projectm_pcm_add_float(projectm_handle, const float *, unsigned int, int);
typedef void (*pm_log_cb)(const char *, int, void *);
void projectm_set_log_callback(pm_log_cb, bool, void *);
void projectm_set_log_level(int, bool);
}

static void logcb(const char *msg, int lvl, void *) { printf("  projectM[%d]: %s\n", lvl, msg ? msg : "?"); }

typedef GLXContext (*CreateCtxAttribs)(Display *, GLXFBConfig, GLXContext, Bool, const int *);

int main(int argc, char **argv)
{
    const int W = 640, H = 480;
    const int frames = argc > 2 ? atoi(argv[2]) : 120;
    const bool useFbo = !(argc > 3 && strcmp(argv[3], "nofbo") == 0);

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) { printf("  no display\n"); return 1; }
    int attribs[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                     GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                     GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, (getenv("PMTEST_NOALPHA") ? 0 : 8), GLX_DEPTH_SIZE, 24,
                     GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, True, None};
    int n = 0;
    GLXFBConfig *cfgs = glXChooseFBConfig(dpy, DefaultScreen(dpy), attribs, &n);
    if (!cfgs || n == 0) { printf("  no fbconfig\n"); return 1; }

    XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, cfgs[0]);
    XSetWindowAttributes swa{};
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
    Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, W, H, 0, vi->depth,
                               InputOutput, vi->visual, CWColormap, &swa);
    XMapWindow(dpy, win);

    auto createCtx = (CreateCtxAttribs)glXGetProcAddress((const GLubyte *)"glXCreateContextAttribsARB");
    // PMTEST_ROBUST=1 adds the robust-access bit, which is the one observed difference
    // between Qt's context (black) and this one (renders) on the same machine.
    int flags = GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
    if (getenv("PMTEST_ROBUST")) flags |= 0x00000004 /* GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB */;
    int ctxAttribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 5,
                        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                        GLX_CONTEXT_FLAGS_ARB, flags, None};
    GLXContext ctx = createCtx(dpy, cfgs[0], nullptr, True, ctxAttribs);
    glXMakeCurrent(dpy, win, ctx);
    printf("  GL: %s | %s | %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

    // A VAO, as reverie now binds, so the two are comparable.
    GLuint vao = 0; glGenVertexArrays(1, &vao); glBindVertexArray(vao);

    GLuint fbo = 0, tex = 0, rbo = 0;
    if (useFbo) {
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glGenRenderbuffers(1, &rbo); glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        printf("  fbo %u status 0x%04x (complete=0x%04x)\n", fbo,
               glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
    }

    projectm_set_log_level(2, false);
    projectm_set_log_callback(logcb, false, nullptr);
    projectm_handle pm = projectm_create();
    if (!pm) { printf("  projectm_create failed\n"); return 1; }
    projectm_set_window_size(pm, W, H);
    if (argc > 1 && strlen(argv[1]) > 0) {
        printf("  loading preset: %s\n", argv[1]);
        projectm_load_preset_file(pm, argv[1], false);
    }

    std::vector<float> pcm(1024);
    int nonBlackBest = 0;
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < 1024; ++i)
            pcm[i] = 0.35f * sinf((f * 1024 + i) * 0.02f) + 0.15f * sinf((f * 1024 + i) * 0.11f);
        projectm_pcm_add_float(pm, pcm.data(), 512, 1 /* PROJECTM_MONO */);
        if (useFbo) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); projectm_opengl_render_frame_fbo(pm, fbo); }
        else projectm_opengl_render_frame(pm);
        GLenum e; while ((e = glGetError()) != GL_NO_ERROR) printf("  GL error after frame %d: 0x%04x\n", f, e);
        if (f % 30 == 29) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, useFbo ? fbo : 0);
            std::vector<unsigned char> px(W * H * 4);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            int nb = 0; long sum = 0;
            for (int i = 0; i < W * H; ++i) { int v = px[i*4]+px[i*4+1]+px[i*4+2]; sum += v; if (v > 12) ++nb; }
            if (nb > nonBlackBest) nonBlackBest = nb;
            printf("  frame %3d: non-black %d/%d (%.1f%%)  mean %.2f\n", f+1, nb, W*H,
                   100.0*nb/(W*H), sum/(double)(W*H*3));
        }
        glXSwapBuffers(dpy, win);
    }
    printf("  RESULT: %s (best non-black %d)\n", nonBlackBest > 0 ? "projectM RENDERS" : "projectM RENDERS NOTHING", nonBlackBest);
    projectm_destroy(pm);
    return 0;
}
