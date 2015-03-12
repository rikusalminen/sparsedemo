#include <string.h>

#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

extern void APIENTRY gl_debug_callback(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void*);

extern unsigned shader_compile(const char *vert, const char *tess_ctrl, const char *tess_eval, const char *geom, const char *frag);

struct gfx {
    unsigned program;

    unsigned vbo;
    unsigned vao;
};

struct gfx gfx_;

static const char *vertex_src = ""
    "#version 450\n"
    "in vec4 pos;"
    "void main() { gl_Position = pos; }";

static const char *frag_src = ""
    "#version 450\n"
    "out vec4 color;"
    "void main() { color = vec4(1.0, 1.0, 1.0, 1.0); }";

int gfx_init(struct gfx *gfx) {
    memset(gfx, 0, sizeof(struct gfx));

    void *debug_data = NULL;
    glDebugMessageCallback(&gl_debug_callback, debug_data);

    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

    float triangle[] = {
        0.0, -1.0, 0.0, 1.0,
        -1.0, 1.0, 0.0, 1.0,
        1.0, 1.0, 0.0, 1.0,
    };

    glGenBuffers(1, &gfx->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gfx->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);

    glGenVertexArrays(1, &gfx->vao);

    glBindVertexArray(gfx->vao);
    glBindBuffer(GL_ARRAY_BUFFER, gfx->vbo);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    glEnableVertexAttribArray(0);

    gfx->program = shader_compile(vertex_src, 0, 0, 0, frag_src);
    if(gfx->program == 0)
        return -1;

    return 0;
}

#include <math.h>

int gfx_paint(struct gfx *gfx, int width, int height, uint64_t frame_number) {
    (void)gfx;
    (void)frame_number;

    glViewport(0, 0, width, height);

    float x = sinf(2.0f*M_PI * (frame_number % 60) / 60.0);

    float clear_color[] = { 0.2*x, 0.4*x, 0.7*x, 1.0*x };
    glClearBufferfv(GL_COLOR, 0, clear_color);

    glUseProgram(gfx->program);
    glBindVertexArray(gfx->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    GLenum glerror = GL_NO_ERROR;
    if((glerror = glGetError()) != GL_NO_ERROR) {
        LOGW("GL error: %X\n", glerror);
        return -1;
    }

    return 0;
}

int gfx_quit(struct gfx *gfx) {
    glDeleteVertexArrays(1, &gfx->vao);
    glDeleteBuffers(1, &gfx->vbo);

    glDeleteProgram(gfx->program);

    return glGetError() == GL_NO_ERROR ? 0 : -1;
}
