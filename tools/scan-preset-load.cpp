// Does projectM accept this preset at all?
//
// Distinct from tools/scan-presets.sh, which renders each preset and measures how much of the
// frame is non-black. That scan cannot see this fault: when a preset fails to compile, projectM
// keeps the *previous* one on screen, so the probe reads a perfectly healthy frame and scores the
// broken preset as fine. The visible symptom is a click in the browser that does nothing.
//
// One GLX context, one projectM instance, every preset loaded in turn. The framebuffer is
// deliberately tiny: a syntax error is found while compiling, not while drawing, so resolution
// buys nothing and costs the whole run.
//
// Reads preset paths on stdin, writes "path<TAB>OK|FAIL<TAB>detail" on stdout.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glx.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <iostream>

extern "C" {
typedef void *projectm_handle;
projectm_handle projectm_create(void);
void projectm_destroy(projectm_handle);
void projectm_set_window_size(projectm_handle, size_t, size_t);
void projectm_opengl_render_frame_fbo(projectm_handle, uint32_t);
void projectm_load_preset_file(projectm_handle, const char *, bool);
typedef void (*pm_log_cb)(const char *, int, void *);
void projectm_set_log_callback(pm_log_cb, bool, void *);
}

static std::string g_detail;
static bool g_failed = false;

// projectM reports a rejected preset only through its log. The strings below are the ones that
// mean "this preset will not run"; anything else it says is commentary.
static void logcb(const char *msg, int, void *)
{
    if (!msg)
        return;
    const std::string m(msg);
    static const char *fatal[] = {"Could not compile", "Could not load", "syntax error",
                                  "Failed to parse", "Could not parse", nullptr};
    for (int i = 0; fatal[i]; ++i) {
        if (m.find(fatal[i]) != std::string::npos) {
            if (!g_failed) {
                g_failed = true;
                g_detail = m;
                for (char &c : g_detail)
                    if (c == '\t' || c == '\n')
                        c = ' ';
            }
            return;
        }
    }
}

typedef GLXContext (*CreateCtxAttribs)(Display *, GLXFBConfig, GLXContext, Bool, const int *);

int main()
{
    const int W = 32, H = 32;
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) { fprintf(stderr, "no display\n"); return 1; }
    int attribs[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                     GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
                     GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24,
                     GLX_STENCIL_SIZE, 8, GLX_DOUBLEBUFFER, True, None};
    int n = 0;
    GLXFBConfig *cfgs = glXChooseFBConfig(dpy, DefaultScreen(dpy), attribs, &n);
    if (!cfgs || n == 0) { fprintf(stderr, "no fbconfig\n"); return 1; }
    XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, cfgs[0]);
    XSetWindowAttributes swa{};
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
    Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, W, H, 0, vi->depth,
                               InputOutput, vi->visual, CWColormap, &swa);
    XMapWindow(dpy, win);
    auto createCtx = (CreateCtxAttribs)glXGetProcAddress((const GLubyte *)"glXCreateContextAttribsARB");
    int ctxAttribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 5,
                        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB, None};
    GLXContext ctx = createCtx(dpy, cfgs[0], nullptr, True, ctxAttribs);
    glXMakeCurrent(dpy, win, ctx);
    GLuint vao = 0; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    GLuint fbo = 0, tex = 0, rbo = 0;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &rbo); glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    projectm_set_log_callback(logcb, true, nullptr);
    projectm_handle pm = projectm_create();
    if (!pm) { fprintf(stderr, "projectm_create failed\n"); return 1; }
    projectm_set_window_size(pm, W, H);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        // projectM ignores a path it does not recognise as a preset without saying so, which
        // would score a typo or a missing file as healthy - a check that cannot fail. Ask the
        // filesystem first.
        if (FILE *f = fopen(line.c_str(), "rb")) {
            fclose(f);
        } else {
            printf("%s\tMISSING\tno such file\n", line.c_str());
            fflush(stdout);
            continue;
        }
        g_failed = false;
        g_detail.clear();
        projectm_load_preset_file(pm, line.c_str(), true);
        // Two frames: the warp and composite shaders are compiled on first use, not at load.
        for (int f = 0; f < 2; ++f)
            projectm_opengl_render_frame_fbo(pm, fbo);
        printf("%s\t%s\t%s\n", line.c_str(), g_failed ? "FAIL" : "OK", g_detail.c_str());
        fflush(stdout);
    }
    projectm_destroy(pm);
    return 0;
}
