#include <string.h>

#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

extern void APIENTRY gl_debug_callback(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void*);

struct gfx {
};

struct gfx gfx_;

int gfx_init(struct gfx *gfx) {
    memset(gfx, 0, sizeof(struct gfx));

    void *debug_data = NULL;
    glDebugMessageCallback(&gl_debug_callback, debug_data);

    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

    return 0;
}

int gfx_paint(struct gfx *gfx, uint64_t frame_number) {
    (void)gfx;
    (void)frame_number;

    float clear_color[] = { 0.2, 0.4, 0.7, 1.0 };
    glClearBufferfv(GL_COLOR, 0, clear_color);

    GLenum glerror = GL_NO_ERROR;
    if((glerror = glGetError()) != GL_NO_ERROR) {
        LOGW("GL error: %X\n", glerror);
        return -1;
    }

    return 0;
}

int gfx_quit(struct gfx *gfx) {
    (void)gfx;

    return 0;
}
