#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

extern void APIENTRY gl_debug_callback(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void*);

extern unsigned shader_compile(const char *vert, const char *tess_ctrl, const char *tess_eval, const char *geom, const char *frag);

struct texmmap;
void* texmmap_ptr(struct texmmap* texmmap);
uint64_t texmmap_size(const struct texmmap *texmmap);

struct painter_state {
    float scroll_x, scroll_y;
    float scroll_vx, scroll_vy;
};

struct astc_header
{
        uint8_t magic[4];
        uint8_t blockdim_x;
        uint8_t blockdim_y;
        uint8_t blockdim_z;
        uint8_t xsize[3];
        uint8_t ysize[3];
        uint8_t zsize[3];
};

#define XFER_NUM_BUFFERS (8)
#define XFER_BUFFER_SIZE (2 * 1024*1024)

#define XFER_NUM_QUEUES         4
#define XFER_QUEUE_IDLE         0
#define XFER_QUEUE_READ         1
#define XFER_QUEUE_UPLOAD       2
#define XFER_QUEUE_WAIT         3

#define XFER_QUEUE_MAX_SIZE  (XFER_NUM_BUFFERS+1) // XXX: queue must never get full!

#define XFER_NUM_THREADS        4

struct xfer_buffer {
    uint64_t size;
    void *pbo_buffer;
    unsigned pbo;

    GLsync syncpt; // NOTE: opaque pointer

    void *src_ptr;
    int tex_format;

    int src_x, src_y;
    int width, height;
    int src_pitch;

    unsigned dst_tex;
    int dst_x, dst_y;

    int page_width, page_height, page_depth;
    int block_width, block_height, block_size;
};

struct xfer_queue {
    pthread_mutex_t queue_lock;
    int stopped;

    int queues[XFER_NUM_QUEUES][XFER_QUEUE_MAX_SIZE];
    int queue_counters[XFER_NUM_QUEUES][2];
    int queue_waiting[XFER_NUM_QUEUES];
    pthread_cond_t queue_not_empty[XFER_NUM_QUEUES];
};

struct xfer {
    struct xfer_buffer buffers[XFER_NUM_BUFFERS];
    struct xfer_queue queue;

    pthread_t threads[XFER_NUM_THREADS];
};

struct gfx {
    unsigned program;

    unsigned vbo;
    unsigned vao;

    unsigned texture;

    struct texmmap *texmmap;

    int tex_format;
    int tex_width, tex_height;
    int page_width, page_height, page_depth;
    int block_width, block_height, block_size;

    struct xfer xfer;
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
    "layout(location = 1) uniform int scroll_x;"
    "layout(location = 2) uniform int scroll_y;"
    "out vec4 color;"
    "void main() {"
        "ivec2 tex_size = textureSize(tex, 0);"
        "ivec2 tex_coord = ivec2(gl_FragCoord.x + scroll_x, gl_FragCoord.y + scroll_y);"
        "if(tex_coord.x > tex_size.x || tex_coord.y > tex_size.y ||"
        "       tex_coord.x < 0 || tex_coord.y < 0) discard;"
        "vec4 texel = vec4(0.0, 1.0, 1.0, 1.0);"
        "int code = sparseTexelFetchEXT(tex, tex_coord, 0, texel);"
        "if(sparseTexelsResidentEXT(code)) color = texel;"
        "else color = vec4(1.0, 1.0, 0.0, 1.0);"
    "}";

static int blockblit2d(
    const void *src, int src_pitch,
    int src_x, int src_y,
    void *dst, int dst_pitch,
    int block_width, int block_height, int block_size,
    int width, int height) {

    int cols = width / block_width;
    int rows = height / block_height;

    for(int row = 0; row < rows; ++row) {
        memcpy((uint8_t*)dst + row*dst_pitch,
            (const uint8_t*)src +
                (src_y/block_height + row)*src_pitch +
                (src_x/block_width)*block_size,
            cols * block_size);
    }

    return rows * cols;
}

static int xfer_buffer_init(struct xfer_buffer *xfer_buffer, uint64_t xfer_size) {
    xfer_buffer->size = xfer_size;
    xfer_buffer->syncpt = 0;

    GLbitfield storage_flags =
        GL_CLIENT_STORAGE_BIT |
        GL_MAP_WRITE_BIT |
        GL_MAP_PERSISTENT_BIT |
        GL_MAP_COHERENT_BIT;

    glGenBuffers(1, &xfer_buffer->pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, xfer_buffer->pbo);
    glBufferStorage(GL_PIXEL_UNPACK_BUFFER, xfer_size, NULL, storage_flags);

    GLbitfield map_flags =
        GL_MAP_WRITE_BIT |
        GL_MAP_PERSISTENT_BIT |
        GL_MAP_COHERENT_BIT;
    void *ptr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, xfer_size, map_flags);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    xfer_buffer->pbo_buffer = ptr;

    return 0;
}

static int xfer_start(
    struct xfer_buffer *xfer_buffer,
    unsigned dst_tex,
    unsigned tex_format,
    void *src_ptr, int src_pitch,
    int src_x, int src_y,
    int dst_x, int dst_y,
    int block_width, int block_height, int block_size,
    int width, int height) {

    uint64_t size_bytes = width/block_width * height/block_height * block_size/8;

    assert(size_bytes < xfer_buffer->size);
    assert(xfer_buffer->syncpt == 0);

    xfer_buffer->dst_tex = dst_tex;
    xfer_buffer->tex_format = tex_format;

    xfer_buffer->src_ptr = src_ptr;
    xfer_buffer->src_pitch = src_pitch;

    xfer_buffer->src_x = src_x; xfer_buffer->src_y = src_y;
    xfer_buffer->dst_x = dst_x; xfer_buffer->dst_y = dst_y;

    xfer_buffer->block_width = block_width;
    xfer_buffer->block_height = block_height;
    xfer_buffer->block_size = block_size;
    xfer_buffer->width = width;
    xfer_buffer->height = height;

    return 0;
}

static int xfer_buffer_blit(struct xfer_buffer *xfer_buffer) {
    int dst_pitch = (xfer_buffer->width / xfer_buffer->block_width) * (xfer_buffer->block_size/8);

    blockblit2d(
        xfer_buffer->src_ptr, xfer_buffer->src_pitch,
        xfer_buffer->src_x, xfer_buffer->src_y,
        xfer_buffer->pbo_buffer, dst_pitch,
        xfer_buffer->block_width, xfer_buffer->block_height, (xfer_buffer->block_size/8),
        xfer_buffer->width, xfer_buffer->height);

    return 0;
}

static int xfer_buffer_upload(struct xfer_buffer *xfer_buffer) {
    glBindTexture(GL_TEXTURE_2D, xfer_buffer->dst_tex);

    glTexPageCommitmentARB(
        GL_TEXTURE_2D,
        0, // XXX: dst_level
        xfer_buffer->dst_x, xfer_buffer->dst_y, 0, // XXX: xfer_buffer->dst_z
        xfer_buffer->width, xfer_buffer->height, 1, // XXX: xfer_buffer->depth
        GL_TRUE);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, xfer_buffer->pbo);

    uint64_t bytes = xfer_buffer->width/xfer_buffer->block_width *
        xfer_buffer->height/xfer_buffer->block_height *
        xfer_buffer->block_size/8;
    glCompressedTexSubImage2D(
        GL_TEXTURE_2D,
        0, // XXX: dst_level
        xfer_buffer->dst_x, xfer_buffer->dst_y,
        xfer_buffer->width,
        xfer_buffer->height,
        xfer_buffer->tex_format,
        bytes,
        NULL);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    GLbitfield fence_flags = 0; // must be zero
    GLsync syncpt = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, fence_flags);

    xfer_buffer->syncpt = syncpt;

    return 0;
}

static int xfer_buffer_finish(
    struct xfer_buffer *xfer_buffer,
    int server_wait,
    int client_wait, int flush, uint64_t client_timeout_ns) {
    int status = 0;

    if(client_wait) {
        GLenum cond = glClientWaitSync(
            xfer_buffer->syncpt,
            flush ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
            client_timeout_ns);

        switch(cond) {
            case GL_ALREADY_SIGNALED:
            case GL_CONDITION_SATISFIED:
                status = 1;
                break;

            case GL_TIMEOUT_EXPIRED:
                status = 0;

            case GL_WAIT_FAILED: // fall through
            default:
                status = -1;
                break;
        }
    }

    if(server_wait) {
        glWaitSync(xfer_buffer->syncpt, 0 /* must be zero */, GL_TIMEOUT_IGNORED);
    }

    if(!client_wait && status == 0) {
        int sync_status = 0;
        glGetSynciv(xfer_buffer->syncpt, GL_SYNC_STATUS, sizeof(int), NULL, &sync_status);

        switch(sync_status) {
            case GL_SIGNALED:
                status = 1;
                break;

            case GL_UNSIGNALED:
                status = 0;
                break;

            default:
                status = -1;
        }
    }

    if(status == 1) {
        glDeleteSync(xfer_buffer->syncpt);
        xfer_buffer->syncpt = 0;
    }

    return status;
}

static int xfer_buffer_free(struct xfer_buffer *xfer_buffer) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, xfer_buffer->pbo);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glDeleteBuffers(1, &xfer_buffer->pbo);

    glDeleteSync(xfer_buffer->syncpt);

    return 0;
}

static int xfer_queue_stop(struct xfer_queue *queue) {
    pthread_mutex_lock(&queue->queue_lock);

    queue->stopped = 1;
    for(int i = 0; i < XFER_NUM_QUEUES; ++i)
        pthread_cond_broadcast(&queue->queue_not_empty[i]);

    pthread_mutex_unlock(&queue->queue_lock);

    return 0;
}

static int xfer_queue_get(
    struct xfer_queue *queue,
    int queue_num,
    int wait,
    int *output, int max_out) {
    assert(output && max_out);

    pthread_mutex_lock(&queue->queue_lock);

    int rd = -1, wr = -1;
    int got = 0;
    int stopped = 0;
    while(!stopped) {
        rd = queue->queue_counters[queue_num][0];
        wr = queue->queue_counters[queue_num][1];
        stopped = queue->stopped;

        if(stopped) {
            break;
        } else if(rd == wr) { // queue is empty
            if(wait) {
                queue->queue_waiting[queue_num] += 1;
                pthread_cond_wait(&queue->queue_not_empty[queue_num], &queue->queue_lock);
                queue->queue_waiting[queue_num] -= 1;
            } else {
                break;
            }
        } else {
            while(rd != wr && got < max_out) {
                int thing = queue->queues[queue_num][rd];
                queue->queues[queue_num][rd] = -1;
                output[got] = thing;

                got += 1;
                rd = (rd+1) % XFER_QUEUE_MAX_SIZE;
            }

            queue->queue_counters[queue_num][0] = rd;
            break;
        }
    }

    pthread_mutex_unlock(&queue->queue_lock);

    return stopped ? -1 : got;
}

static int xfer_queue_put(struct xfer_queue *queue, int queue_num, int element) {
    pthread_mutex_lock(&queue->queue_lock);

    int rd = queue->queue_counters[queue_num][0];
    int wr = queue->queue_counters[queue_num][1];
    int next = (wr + 1) % XFER_QUEUE_MAX_SIZE;
    int stopped = queue->stopped;

    int result = 0;
    if(!stopped && next != rd) { // queue not full
        queue->queues[queue_num][wr] = element;
        queue->queue_counters[queue_num][1] = next;

        result = 1;

        if(queue->queue_waiting[queue_num] > 0)
            pthread_cond_signal(&queue->queue_not_empty[queue_num]);
    }

    pthread_mutex_unlock(&queue->queue_lock);

    return stopped ? -1 : result;
}

static void* xfer_thread_main(void *arg) {
    struct xfer *xfer = (struct xfer*)arg;

    int buffer_id = -1;
    while(xfer_queue_get(&xfer->queue, XFER_QUEUE_READ, 1, &buffer_id, 1) == 1) {
        LOGI("**** BLITTING BUFFER: %d", buffer_id);
        struct xfer_buffer *xfer_buffer = &xfer->buffers[buffer_id];
        xfer_buffer_blit(xfer_buffer);

        if(xfer_queue_put(&xfer->queue, XFER_QUEUE_UPLOAD, buffer_id) != 1)
            break;
    }

    return (void*)xfer;
}

static int xfer_init(struct xfer *xfer, int buffer_size) {
    for(int i = 0; i < XFER_NUM_BUFFERS; ++i) {
        LOGI("**** INIT BUFFER: %d / %d", i, XFER_NUM_BUFFERS);
        if(xfer_buffer_init(&xfer->buffers[i], buffer_size) != 0)
            return 0;
    }

    for(int i = 0; i < XFER_NUM_QUEUES; ++i)
        for(int j = 0; j < XFER_QUEUE_MAX_SIZE; ++j)
            xfer->queue.queues[i][j] = -1;

    for(int i = 0; i < XFER_NUM_BUFFERS; ++i) // initialize pending queue
        xfer->queue.queues[XFER_QUEUE_IDLE][i] = i;
    xfer->queue.queue_counters[XFER_QUEUE_IDLE][1] = XFER_NUM_BUFFERS;

    for(int i = 0; i < XFER_NUM_THREADS; ++i)
        pthread_create(&xfer->threads[i], NULL, xfer_thread_main, (void*)xfer);

    return 0;
}

static int xfer_free(struct xfer *xfer) {
    xfer_queue_stop(&xfer->queue);

    int err;
    for(int i = 0; i < XFER_NUM_THREADS; ++i) {
        void *ret;
        pthread_join(xfer->threads[i], &ret);
        if(ret == 0)
            err = -1;
    }

    for(int i = 0; i < XFER_NUM_BUFFERS; ++i)
        xfer_buffer_free(&xfer->buffers[i]);

    return err;
}

static int xfer_upload(struct xfer *xfer, int wait) {
    int queue[XFER_QUEUE_MAX_SIZE];
    int num = xfer_queue_get(&xfer->queue, XFER_QUEUE_UPLOAD, wait,  queue, XFER_QUEUE_MAX_SIZE);

    for(int i = 0; i < num; ++i) {
        int buffer_id = queue[i];
        struct xfer_buffer *xfer_buffer = &xfer->buffers[buffer_id];

        LOGI("**** UPLOADING BUFFER: %d", buffer_id);
        xfer_buffer_upload(xfer_buffer);

        xfer_queue_put(&xfer->queue, XFER_QUEUE_WAIT, buffer_id);
    }

    return num;
}

static int xfer_finish(struct xfer *xfer) {
    int queue[XFER_QUEUE_MAX_SIZE];
    int num = xfer_queue_get(&xfer->queue, XFER_QUEUE_WAIT, 0, queue, XFER_QUEUE_MAX_SIZE);

    int num_finished = 0;
    for(int i = 0; i < num; ++i) {
        int buffer_id = queue[i];
        struct xfer_buffer *xfer_buffer = &xfer->buffers[buffer_id];

        LOGI("**** FINISHING BUFFER: %d", buffer_id);

        int finished = xfer_buffer_finish(xfer_buffer, 1, 0, 0, 0);
        if(finished) {
            xfer_queue_put(&xfer->queue, XFER_QUEUE_IDLE, buffer_id);
            num_finished += 1;
        } else {
            xfer_queue_put(&xfer->queue, XFER_QUEUE_WAIT, buffer_id);
        }
    }

    return num_finished;
}

static int gfx_page_commit(struct gfx *gfx, int page_x, int page_y) {
    int level = 0;

    glTexPageCommitmentARB(
        GL_TEXTURE_2D,
        level,
        page_x * gfx->page_width, page_y * gfx->page_height, 0,
        gfx->page_width, gfx->page_height, gfx->page_depth,
        GL_TRUE);

    const void *texptr = texmmap_ptr(gfx->texmmap);
    const uint64_t header_size = 16;
    const char *ptr = (const char *)texptr + header_size;

    char pagebuffer[64*1024];
    int src_pitch = (gfx->tex_width/gfx->block_width) * (gfx->block_size/8);
    int dst_pitch = (512/8) * (gfx->block_size/8);

    blockblit2d(ptr, src_pitch,
        page_x * gfx->page_width, page_y * gfx->page_height,
        pagebuffer, dst_pitch,
        gfx->block_width, gfx->block_height, (gfx->block_size/8),
        gfx->page_width, gfx->page_height);

    glCompressedTexSubImage2D(
        GL_TEXTURE_2D,
        level,
        page_x * gfx->page_width, page_y * gfx->page_height,
        gfx->page_width, gfx->page_height,
        gfx->tex_format, 64*1024,
        pagebuffer);

    return 0;
}

static int gfx_page_uncommit(struct gfx *gfx, int page_x, int page_y) {
    int level = 0;

    glTexPageCommitmentARB(
        GL_TEXTURE_2D,
        level,
        page_x * gfx->page_width, page_y * gfx->page_height, 0,
        gfx->page_width, gfx->page_height, gfx->page_depth,
        GL_FALSE);

    return 0;
}

int gfx_init(struct gfx *gfx, struct texmmap *texmmap) {
    memset(gfx, 0, sizeof(struct gfx));
    gfx->texmmap = texmmap;

    if(!texmmap_ptr(gfx->texmmap))
        return -1;

    void *debug_data = NULL;
    glDebugMessageCallback(&gl_debug_callback, debug_data);

    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

    if(xfer_init(&gfx->xfer, XFER_BUFFER_SIZE) != 0)
        return -1;

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

    void *texptr = texmmap_ptr(gfx->texmmap);
    uint64_t texsize = texmmap_size(gfx->texmmap);
    (void)texsize; // XXX: check that size is ok

    const struct astc_header *header = (const struct astc_header*)texptr;
    int w = header->xsize[0] + (header->xsize[1] << 8) + (header->xsize[2] << 16);
    int h = header->ysize[0] + (header->ysize[1] << 8) + (header->ysize[2] << 16);
    int d = header->zsize[0] + (header->zsize[1] << 8) + (header->zsize[2] << 16);
    (void)d; // XXX: depth must be 1

    glGenTextures(1, &gfx->texture);
    glBindTexture(GL_TEXTURE_2D, gfx->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SPARSE_ARB, GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_VIRTUAL_PAGE_SIZE_INDEX_ARB, pgsz_index);

    int tex_width = w, tex_height = h;
    int levels = 1;
    glTexStorage2D(GL_TEXTURE_2D, levels, tex_format, tex_width, tex_height);

    gfx->tex_format = tex_format;
    gfx->tex_width = tex_width;
    gfx->tex_height = tex_height;
    gfx->page_width = page_width;
    gfx->page_height = page_height;
    gfx->page_depth = page_depth;
    gfx->block_width = block_width;
    gfx->block_height = block_height;
    gfx->block_size = block_size;

    if(0) {
        gfx_page_commit(gfx, 0, 0);
        gfx_page_commit(gfx, 1, 0);
        gfx_page_commit(gfx, 2, 0);
        gfx_page_commit(gfx, 0, 1);
        gfx_page_commit(gfx, 1, 1);
        gfx_page_commit(gfx, 2, 1);

        gfx_page_uncommit(gfx, 1, 1);
        gfx_page_commit(gfx, 1, 1);
    }

    if(0) {
        struct xfer_buffer *xfer_buffer = &gfx->xfer.buffers[0];

        int src_pitch = (gfx->tex_width/gfx->block_width) * (gfx->block_size/8);

        xfer_start(
            xfer_buffer,
            gfx->texture, gfx->tex_format,
            texmmap_ptr(gfx->texmmap), src_pitch,
            0 * gfx->page_width, 0 * page_height,
            0, 0,
            gfx->block_width, gfx->block_height, gfx->block_size,
            4 * gfx->page_width, 4 * gfx->page_width);

        xfer_buffer_blit(xfer_buffer);
        xfer_buffer_upload(xfer_buffer);

        xfer_buffer_finish(xfer_buffer, 1, 0, 0, 0);
    }

    int pages_x = 3, pages_y = 2;
    for(int i = 0; i < pages_x*pages_y; ++i) {
        LOGI("**** GET IDLE BUFFER");

        //if(i == 1) continue;

        int buffer_id = -1;
        xfer_queue_get(&gfx->xfer.queue, XFER_QUEUE_IDLE, 1, &buffer_id, 1);
        struct xfer_buffer *xfer_buffer = &gfx->xfer.buffers[buffer_id];

        LOGI("**** STARTING BUFFER: %d", buffer_id);

        int src_pitch = (gfx->tex_width/gfx->block_width) * (gfx->block_size/8);
        xfer_start(
            xfer_buffer,
            gfx->texture, gfx->tex_format,
            texmmap_ptr(gfx->texmmap), src_pitch,
            (i%pages_x) * gfx->page_width, (i/pages_x) * page_height,
            (i%pages_x) * gfx->page_width, (i/pages_x) * page_height,
            gfx->block_width, gfx->block_height, gfx->block_size,
            1 * gfx->page_width, 1 * gfx->page_width);

        xfer_queue_put(&gfx->xfer.queue, XFER_QUEUE_READ, buffer_id);

        LOGI("**** STARTED BUFFER: %d", buffer_id);
    }

    return 0;
}

#include <math.h>

int gfx_paint(
    struct gfx *gfx,
    const struct painter_state *state,
    int width, int height,
    uint64_t frame_number) {
    (void)frame_number;

    int num_finished = xfer_finish(&gfx->xfer); // finish uploads
    if(num_finished > 0)
        LOGI("**** TRANSFERS FINISHED: %d", num_finished);

    glViewport(0, 0, width, height);

    float clear_color[] = { 0.2, 0.4, 0.7, 1.0 };
    glClearBufferfv(GL_COLOR, 0, clear_color);

    glUseProgram(gfx->program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gfx->texture);
    glUniform1i(0, 0);

    glUniform1i(1, state->scroll_x);
    glUniform1i(2, state->scroll_y);

    glBindVertexArray(gfx->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLenum glerror = GL_NO_ERROR;
    if((glerror = glGetError()) != GL_NO_ERROR) {
        LOGW("GL error: %X\n", glerror);
        return -1;
    }

    xfer_upload(&gfx->xfer, 0); // start new uploads

    return 0;
}

int gfx_quit(struct gfx *gfx) {
    xfer_free(&gfx->xfer);

    glDeleteVertexArrays(1, &gfx->vao);
    glDeleteBuffers(1, &gfx->vbo);

    glDeleteProgram(gfx->program);

    return glGetError() == GL_NO_ERROR ? 0 : -1;
}
