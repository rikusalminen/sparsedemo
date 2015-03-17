#include <stdint.h>
#include <string.h>

#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

extern void APIENTRY gl_debug_callback(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void*);

extern unsigned shader_compile(const char *vert, const char *tess_ctrl, const char *tess_eval, const char *geom, const char *frag);

struct texmmap;
void* texmmap_ptr(struct texmmap* texmmap);
uint64_t texmmap_size(const struct texmmap *texmmap);

struct gfx {
    unsigned program;

    unsigned vbo;
    unsigned vao;

    unsigned texture;

    struct texmmap *texmmap;
};

struct gfx gfx_;

static const char *vertex_src = ""
    "#version 450\n"
    //"in vec4 pos;"
    //"void main() { gl_Position = pos; }";

    "void main() {"
        "int u = gl_VertexID >> 1; int v = (gl_VertexID & 1)^1;"
        "gl_Position = vec4(-1.0 + 2.0 * u, -1.0 + 2.0 * v, 0.0, 1.0);"
    "}";

static const char *frag_src = ""
    "#version 450\n"
    "#extension GL_EXT_sparse_texture2 : enable\n"
    "layout(location = 0) uniform sampler2D tex;"
    "out vec4 color;"
    "void main() {"
        "ivec2 tex_size = textureSize(tex, 0);"
        "if(gl_FragCoord.x > tex_size.x || gl_FragCoord.y > tex_size.y) discard;"
        "vec4 texel = vec4(1.0, 0.0, 1.0, 1.0);"
        "ivec2 tex_coord = ivec2(gl_FragCoord.x, gl_FragCoord.y);"
        "int code = sparseTexelFetchEXT(tex, tex_coord, 0, texel);"
        "if(sparseTexelsResidentEXT(code)) color = texel;"
        "else color = vec4(1.0, 1.0, 0.0, 1.0);"
    "}";

int gfx_init(struct gfx *gfx, struct texmmap *texmmap) {
    memset(gfx, 0, sizeof(struct gfx));
    gfx->texmmap = texmmap;

    void *debug_data = NULL;
    glDebugMessageCallback(&gl_debug_callback, debug_data);

    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

    int tex_format = GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
    int pgsz_index = -1;
    int page_width = 0, page_height = 0, page_depth = 0;
    int block_width = 0, block_height = 0, block_size = 0;
    (void)block_width; (void)block_height; (void)block_size;

    int num_compressed_formats;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &num_compressed_formats);

    int compressed_formats[num_compressed_formats];
    glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, compressed_formats);

    LOGI("GL_NUM_COMPRESSED_TEXTURE_FORMATS: %d\n", num_compressed_formats);
    for(int i = 0; i < num_compressed_formats; ++i) {
        int fmt = compressed_formats[i];
        int block_x, block_y, block_sz;

        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_TEXTURE_COMPRESSED_BLOCK_WIDTH,
            sizeof(int), &block_x);
        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_TEXTURE_COMPRESSED_BLOCK_HEIGHT,
            sizeof(int), &block_y);
        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_TEXTURE_COMPRESSED_BLOCK_SIZE,
            sizeof(int), &block_sz);

        int num_page_sizes = 0;

        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_NUM_VIRTUAL_PAGE_SIZES_ARB,
            sizeof(int), &num_page_sizes);

        int page_size_x[num_page_sizes],
            page_size_y[num_page_sizes],
            page_size_z[num_page_sizes];
        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_VIRTUAL_PAGE_SIZE_X_ARB,
            num_page_sizes * sizeof(int), page_size_x);
        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_VIRTUAL_PAGE_SIZE_Y_ARB,
            num_page_sizes * sizeof(int), page_size_y);
        glGetInternalformativ(
            GL_TEXTURE_2D, fmt,
            GL_VIRTUAL_PAGE_SIZE_Z_ARB,
            num_page_sizes * sizeof(int), page_size_z);

        if(tex_format == fmt) {
            pgsz_index = 0;
            page_width = page_size_x[pgsz_index];
            page_height = page_size_y[pgsz_index];
            page_depth = page_size_z[pgsz_index];
            block_width = block_x;
            block_height = block_y;
            block_size = block_sz;
        }

        LOGI("\t%X  block %2d x %2d  (%3d bits):  %d page sizes  (%3d x %3d x %3d)",
            fmt,
            block_x, block_y, block_sz,
            num_page_sizes,
            page_size_x[0], page_size_y[0], page_size_z[0]
            );
    }

    //float triangle[] = {
        //0.0, -1.0, 0.0, 1.0,
        //-1.0, 1.0, 0.0, 1.0,
        //1.0, 1.0, 0.0, 1.0,
    //};

    //glGenBuffers(1, &gfx->vbo);
    //glBindBuffer(GL_ARRAY_BUFFER, gfx->vbo);
    //glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);

    glGenVertexArrays(1, &gfx->vao);

    //glBindVertexArray(gfx->vao);
    //glBindBuffer(GL_ARRAY_BUFFER, gfx->vbo);
    //glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    //glEnableVertexAttribArray(0);

    gfx->program = shader_compile(vertex_src, 0, 0, 0, frag_src);
    if(gfx->program == 0)
        return -1;

    glGenTextures(1, &gfx->texture);
    glBindTexture(GL_TEXTURE_2D, gfx->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SPARSE_ARB, GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_VIRTUAL_PAGE_SIZE_INDEX_ARB, pgsz_index);

    int tex_width = 2 * page_width, tex_height = 2 * page_height;
    glTexStorage2D(GL_TEXTURE_2D, 1, tex_format, tex_width, tex_height);

    int level = 0;
    glTexPageCommitmentARB(
        GL_TEXTURE_2D,
        level,
        0, 0, 0,
        page_width, page_height, page_depth,
        GL_TRUE);

    void *texptr = texmmap_ptr(gfx->texmmap);
    uint64_t texsize = texmmap_size(gfx->texmmap);
    if(texptr) {
        const uint64_t header_size = 16;
        const char *ptr = (const char *)texptr + header_size;
        glCompressedTexSubImage2D(
            GL_TEXTURE_2D,
            level,
            0, 0,
            page_width, page_height,
            tex_format, texsize - header_size,
            ptr);
    }

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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gfx->texture);
    glUniform1i(0, 0);

    glBindVertexArray(gfx->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

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
