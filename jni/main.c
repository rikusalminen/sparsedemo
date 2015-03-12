#include <stdbool.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLXW/glxw.h>

#include <time.h>
#include <pthread.h>

#include <android/input.h>
#include <android/sensor.h>
#include <android/native_activity.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

static const clockid_t clock_id = CLOCK_MONOTONIC;

static EGLDisplay display = 0;
static EGLConfig config = 0;
static int native_format = 0;

struct gfx;
extern struct gfx gfx_;
int gfx_init(struct gfx *gfx);
int gfx_paint(struct gfx *gfx, int width, int height, uint64_t frame_number);
int gfx_quit(struct gfx *gfx);

static struct painter {
    ANativeActivity *native_activity;
    ANativeWindow *native_window;
    EGLSurface surface;
    EGLContext context;

    pthread_mutex_t lock;
    pthread_cond_t state_changed;

    pthread_t painter_thread;

    int stopped, dirty, painting;
} painter_;

static void *painter_main(void *ptr) {
    struct painter *painter = (struct painter*)ptr;

    eglMakeCurrent(display, painter->surface, painter->surface, painter->context);
    eglSwapInterval(display, 1);

    int error = 0;
    if(gfx_init(&gfx_) != 0)
        error = -1;

    uint64_t frame_number = 0;
    uint64_t nanoseconds = 1000000000;
    uint64_t min_interval = 1 * nanoseconds / 150;
    uint64_t last_frame_time = 0;

    uint64_t last_fps_report_time = 0;
    uint64_t last_fps_frame = 0;
    while(error == 0) {
        // lock the mutex and check if repaint is needed
        int stopped = 0;
        pthread_mutex_lock(&painter->lock);

        int waiting = 1;
        while(waiting) {
            if(painter->stopped || painter->dirty) {
                waiting = 0;
            } else if(painter->painting && last_frame_time != 0) {
                uint64_t next_frame = last_frame_time + min_interval;
                struct timespec timeout = {
                    next_frame / nanoseconds, // seconds
                    next_frame % nanoseconds  // nanoseconds
                };

                int err = pthread_cond_timedwait(
                    &painter->state_changed, &painter->lock,
                    &timeout);

                if(err == ETIMEDOUT)
                    waiting = 0;
                else if(err != 0)
                    error = -1;
            } else if(!painter->painting) {
                pthread_cond_wait(&painter->state_changed, &painter->lock);
            }
        }

        stopped = painter->stopped;
        painter->dirty = 0;

        pthread_mutex_unlock(&painter->lock);

        // exit loop if stopped or error has occured
        if(stopped || error != 0)
            break;

        // frame timer clock
        struct timespec frametime;
        clock_gettime(clock_id, &frametime);
        last_frame_time = (uint64_t)frametime.tv_sec * nanoseconds +
            (uint64_t)frametime.tv_nsec;

        // get window size
        //int awidth = ANativeWindow_getWidth(painter->native_window);
        //int aheight = ANativeWindow_getHeight(painter->native_window);
        int width, height;
        eglQuerySurface(display, painter->surface, EGL_WIDTH, &width);
        eglQuerySurface(display, painter->surface, EGL_HEIGHT, &height);

        // paint the screen
        if(gfx_paint(&gfx_, width, height, frame_number) != 0)
            error = -1;
        else
            eglSwapBuffers(display, painter->surface);

        // increase frame number
        frame_number += 1;

        // Report FPS once every 5 seconds
        if(last_fps_report_time + nanoseconds * 5 < last_frame_time) {
            uint64_t frames = frame_number - last_fps_frame;
            uint64_t nanos = last_frame_time - last_fps_report_time;
            uint64_t fps = nanoseconds * frames / nanos;

            last_fps_report_time = last_frame_time;
            last_fps_frame = frame_number;

            LOGI("**** PAINTER FPS: %d\n", (int)fps);
        }
    }

    if(gfx_quit(&gfx_) != 0)
        error = -1;

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if(error != 0)
        ANativeActivity_finish(painter->native_activity); // XXX: clean up?

    return error == 0 ? ptr : NULL;
}

static void painter_start(struct painter *painter) {
    pthread_mutex_init(&painter->lock, NULL);

    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, clock_id);

    pthread_cond_init(&painter->state_changed, &condattr);
    pthread_condattr_destroy(&condattr);

    pthread_create(&painter->painter_thread, NULL, painter_main, painter);
}

static void painter_dirty(struct painter *painter) {
    pthread_mutex_lock(&painter->lock);
    painter->dirty = 1;
    pthread_cond_signal(&painter->state_changed);
    pthread_mutex_unlock(&painter->lock);
}

static void painter_paint(struct painter *painter, int painting) {
    pthread_mutex_lock(&painter->lock);
    painter->painting = painting;

    if(painting)
        pthread_cond_signal(&painter->state_changed);

    pthread_mutex_unlock(&painter->lock);
}

static int painter_stop(struct painter *painter) {
    pthread_mutex_lock(&painter->lock);
    painter->stopped = 1;
    pthread_cond_broadcast(&painter->state_changed);
    pthread_mutex_unlock(&painter->lock);

    void *ret;
    pthread_join(painter->painter_thread, &ret);

    pthread_mutex_destroy(&painter->lock);
    pthread_cond_destroy(&painter->state_changed);

    return ret == (void*)painter ? 0 : -1;
}

static void egl_init()
{
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, 0, 0);

    const int config_attribs[] = {
        EGL_RED_SIZE, 0,
        EGL_GREEN_SIZE, 0,
        EGL_BLUE_SIZE, 0,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLES, 0,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };

    LOGI("EGL_VENDOR: %s", eglQueryString(display, EGL_VENDOR));
    LOGI("EGL_VERSION: %s", eglQueryString(display, EGL_VERSION));
    LOGI("EGL_CLIENT_APIS: %s", eglQueryString(display, EGL_CLIENT_APIS));
    LOGI("EGL_EXTENSIONS: %s", eglQueryString(display, EGL_EXTENSIONS));

    int num_configs;
    eglChooseConfig(display, config_attribs, &config, 1, &num_configs);
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &native_format);

    glxwInit();
}

static void egl_quit()
{
    eglTerminate(display);
}

static void handle_event_key(AInputEvent *event)
{
    LOGI("**** KEY EVENT  Action: %d Flags: %d KeyCode: %d ScanCode: %d MetaState: %x RepeatCount: %d DownTime: %lld EventTime: %lld ", 
        AKeyEvent_getAction(event),
        AKeyEvent_getFlags(event),
        AKeyEvent_getKeyCode(event),
        AKeyEvent_getScanCode(event),
        AKeyEvent_getMetaState(event),
        AKeyEvent_getRepeatCount(event),
        AKeyEvent_getDownTime(event),
        AKeyEvent_getEventTime(event));

    painter_paint(&painter_, 1);
}

static void handle_event_motion(AInputEvent *event)
{
    LOGI("**** MOTION EVENT Action: %d Flags: %d MetaState: %X DownTime: %lld EventTime: %lld",
        AMotionEvent_getAction(event),
        AMotionEvent_getFlags(event),
        AMotionEvent_getMetaState(event),
        AMotionEvent_getDownTime(event),
        AMotionEvent_getEventTime(event));

    size_t pointer_count = AMotionEvent_getPointerCount(event);
    for(size_t pointer_index = 0; pointer_index < pointer_count; ++pointer_index)
    {
        LOGI("****    POINTER: %u  x: %3.3f  y: %3.3f  pressure: %3.3f",
            pointer_index,
            AMotionEvent_getX(event, pointer_index),
            AMotionEvent_getY(event, pointer_index),
            AMotionEvent_getPressure(event, pointer_index));
    }

    painter_paint(&painter_, 0);
}

static void handle_event(AInputEvent *event)
{
    switch(AInputEvent_getType(event))
    {
        case AINPUT_EVENT_TYPE_KEY:
            handle_event_key(event);
            return;
        case AINPUT_EVENT_TYPE_MOTION:
            handle_event_motion(event);
            return;
        default:
            break;
    }
}

static int input_callback(int fd, int events, void* data)
{
    (void)fd;
    (void)events;
    AInputQueue* queue = (AInputQueue*)data;

    int32_t has_events = AInputQueue_hasEvents(queue);
    if(has_events < 0)
        LOGW("**** AInputQueue_hasEvents FAILED");

    if(has_events > 0)
    {
        AInputEvent* event;
        if(AInputQueue_getEvent(queue, &event) < 0)
            LOGW("**** AInputQueue_getEvent FAILED");
        else
        {
            if(AInputQueue_preDispatchEvent(queue, event) == 0)
            {
                int handled = 0;
                handle_event(event);
                AInputQueue_finishEvent(queue, event, handled);
            }
        }
    }

    return 1;
}

static int sensor_callback(int fd, int events, void *data)
{
    (void)fd;
    (void)events;
    (void)data;

    return 1;
}

static void onStart(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onStart");
}

static void onResume(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onResume");
}

static void* onSaveInstanceState(ANativeActivity* activity, size_t* outSize)
{
    (void)activity;
    LOGI("ANativeActivity onSaveInstanceState");

    *outSize = 0;
    return 0;
}

static void onPause(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onPause");
}

static void onStop(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onStop");
}

static void onDestroy(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onDestroy");

    egl_quit();
}

static ASensorEventQueue *sensor_eventq;

static void onWindowFocusChanged(ANativeActivity* activity, int hasFocus)
{
    (void)activity;
    LOGI("ANativeActivity onWindowFocusChanged");

    ASensorManager *sensor_manager = ASensorManager_getInstance();

    if(hasFocus)
    {
        ASensorList sensor_list = 0;
        int xxx = ASensorManager_getSensorList(sensor_manager, &sensor_list);
        LOGI("******* SENSORS: %d", xxx);
        for(int i = 0; i < xxx; ++i)
        {
            int sensor_type = ASensor_getType(sensor_list[i]);
            int min_delay = ASensor_getMinDelay(sensor_list[i]);
            ASensor const *default_sensor = ASensorManager_getDefaultSensor(sensor_manager, sensor_type);
            int is_default = (default_sensor == sensor_list[i]);
            LOGI("****   SENSOR %p  %s %s  type: %d  min_delay: %d%s", sensor_list[i], ASensor_getVendor(sensor_list[i]), ASensor_getName(sensor_list[i]), sensor_type, min_delay, is_default ? " DEFAULT" : "");

            if(!is_default)
                continue;
        }

        (void)sensor_eventq;
        (void)sensor_callback;

    }

}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* native_window)
{
    LOGI("ANativeActivity onNativeWindowCreated");

    //ANativeWindow_acquire(window);
    //ANativeWindow_release(window);
   //
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, native_format);

    EGLSurface surface = eglCreateWindowSurface(display, config, native_window, NULL);

    const int context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 5,
        EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
        EGL_NONE
    };
    eglBindAPI(EGL_OPENGL_API);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);

    struct painter *painter = (struct painter*)activity->instance;
    memset(painter, 0, sizeof(*painter));
    painter->native_window = native_window;
    painter->context = context;
    painter->surface = surface;
    painter_start(painter);

}

static void onNativeWindowResized(ANativeActivity* activity, ANativeWindow* native_window)
{
    (void)activity;
    LOGI("ANativeActivity onNativeWindowResized: %p", native_window);
}

static void onNativeWindowRedrawNeeded(ANativeActivity* activity, ANativeWindow* native_window)
{
    LOGI("ANativeActivity onNativeWindowRedrawNeeded: %p", native_window);

    struct painter *painter = (struct painter*)activity->instance;
    painter_dirty(painter);
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* native_window)
{
    LOGI("ANativeActivity onNativeWindowDestroyed: %p", native_window);

    struct painter *painter = (struct painter*)activity->instance;
    painter_stop(painter);

    eglDestroySurface(display, painter->surface);
    eglDestroyContext(display, painter->context);
}

static void onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue)
{
    (void)activity;
    LOGI("ANativeActivity onInputQueueCreated");

    ALooper* looper = ALooper_forThread();
    int ident = ALOOPER_POLL_CALLBACK;
    int (*callback)(int fd, int events, void* data) = input_callback;
    void *data = (void*)queue;
    AInputQueue_attachLooper(queue, looper, ident, callback, data);
}

static void onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue)
{
    (void)activity;
    LOGI("ANativeActivity onInputQueueDestroyed");

    AInputQueue_detachLooper(queue);
}

static void onContentRectChanged(ANativeActivity* activity, const ARect* rect)
{
    (void)activity;
    (void)rect;
    LOGI("ANativeActivity onContentRectChanged");
}

static void onConfigurationChanged(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onConfigurationChanged");
}

static void onLowMemory(ANativeActivity* activity)
{
    (void)activity;
    LOGI("ANativeActivity onLowMemory");
}

void ANativeActivity_onCreate(
    ANativeActivity* activity,
    void* saved_state,
    size_t saved_state_size)
{
    (void)saved_state;
    (void)saved_state_size;

    activity->callbacks->onStart = onStart;
    activity->callbacks->onResume = onResume;
    activity->callbacks->onSaveInstanceState = onSaveInstanceState;
    activity->callbacks->onPause = onPause;
    activity->callbacks->onStop = onStop;
    activity->callbacks->onDestroy = onDestroy;
    activity->callbacks->onWindowFocusChanged = onWindowFocusChanged;
    activity->callbacks->onNativeWindowCreated = onNativeWindowCreated;
    activity->callbacks->onNativeWindowResized = onNativeWindowResized;
    activity->callbacks->onNativeWindowRedrawNeeded = onNativeWindowRedrawNeeded;
    activity->callbacks->onNativeWindowDestroyed = onNativeWindowDestroyed;
    activity->callbacks->onInputQueueCreated = onInputQueueCreated;
    activity->callbacks->onInputQueueDestroyed = onInputQueueDestroyed;
    activity->callbacks->onContentRectChanged = onContentRectChanged;
    activity->callbacks->onConfigurationChanged = onConfigurationChanged;
    activity->callbacks->onLowMemory = onLowMemory;

    LOGI("*** internal data path: %s\n", activity->internalDataPath);
    LOGI("*** external data path: %s\n", activity->externalDataPath);

    memset(&painter_, 0, sizeof(painter_));
    painter_.native_activity = activity;
    activity->instance = &painter_;

    egl_init();
}
