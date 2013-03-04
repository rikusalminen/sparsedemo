#include <stdbool.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <android/input.h>
#include <android/sensor.h>
#include <android/native_activity.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

static EGLDisplay display = 0;
static EGLSurface surface = 0;
static EGLContext context = 0;
static EGLConfig config = 0;
static int format = 0;

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
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    LOGI("EGL_VENDOR: %s", eglQueryString(display, EGL_VENDOR));
    LOGI("EGL_VERSION: %s", eglQueryString(display, EGL_VERSION));
    LOGI("EGL_CLIENT_APIS: %s", eglQueryString(display, EGL_CLIENT_APIS));
    LOGI("EGL_EXTENSIONS: %s", eglQueryString(display, EGL_EXTENSIONS));

    int num_configs;
    eglChooseConfig(display, config_attribs, &config, 1, &num_configs);

    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

    const int context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    eglBindAPI(EGL_OPENGL_ES_API);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);

}

static void egl_quit()
{
    eglDestroyContext(display, context);
    eglTerminate(display);
}

static void gles_init(ANativeWindow *native_window)
{
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, format);

    surface = eglCreateWindowSurface(display, config, native_window, NULL);
}

static void gles_quit()
{
    eglDestroySurface(display, surface);
}

static void gles_paint()
{
    eglMakeCurrent(display, surface, surface, context);

    //LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    //LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    //LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    //LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

    glClearColor(0.2, 0.4, 0.7, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    eglSwapBuffers(display, surface);

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    (void)window;
    LOGI("ANativeActivity onNativeWindowCreated");

    gles_init(window);
    //gles_paint();

    //ANativeWindow_acquire(window);
    //ANativeWindow_release(window);

    LOGI("**** NATIVE WINDOW: %p\n", window);

}

static void onNativeWindowResized(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    (void)window;
    LOGI("ANativeActivity onNativeWindowResized");
}

static void onNativeWindowRedrawNeeded(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    (void)window;
    LOGI("ANativeActivity onNativeWindowRedrawNeeded");

    gles_paint();
}

static void onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    (void)window;
    LOGI("ANativeActivity onNativeWindowDestroyed");

    LOGI("**** NATIVE WINDOW: %p\n", window);

    gles_quit();
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

    activity->instance = (void*)0xdeadbeef;

    egl_init();
}
