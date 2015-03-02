#include <string.h>

#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

struct gfx {
};

struct gfx gfx_;

int gfx_init(struct gfx *gfx) {
    memset(gfx, 0, sizeof(struct gfx));

    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

    return 0;
}

int gfx_paint(struct gfx *gfx) {
    (void)gfx;

    float clear_color[] = { 0.2, 0.4, 0.7, 1.0 };
    glClearBufferfv(GL_COLOR, 0, clear_color);

    return 0;
}

int gfx_quit(struct gfx *gfx) {
    (void)gfx;

    return 0;
}
