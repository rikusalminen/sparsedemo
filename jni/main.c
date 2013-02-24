#include <stdbool.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <android/input.h>
#include <android/native_activity.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;

static void gles_init(ANativeWindow *native_window)
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

    EGLConfig config;
    int num_configs;
    eglChooseConfig(display, config_attribs, &config, 1, &num_configs);

    int format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(native_window, 0, 0, format);

    surface = eglCreateWindowSurface(display, config, native_window, NULL);

    const int context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    eglBindAPI(EGL_OPENGL_ES_API);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);

    eglMakeCurrent(display, surface, surface, context);

    LOGI("GL_VERSION: %s", glGetString(GL_VERSION));
    LOGI("GL_VENDOR: %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER: %s", glGetString(GL_RENDERER));
    LOGI("GL_EXTENSIONS: %s", glGetString(GL_EXTENSIONS));

}

static void gles_quit()
{
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
}

static void gles_paint()
{
    glClearColor(0.2, 0.4, 0.7, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    eglSwapBuffers(display, surface);
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
}

static void onWindowFocusChanged(ANativeActivity* activity, int hasFocus)
{
    (void)activity;
    (void)hasFocus;
    LOGI("ANativeActivity onWindowFocusChanged");
}

static void onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
    (void)activity;
    (void)window;
    LOGI("ANativeActivity onNativeWindowCreated");

    gles_init(window);
    //gles_paint();
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

    gles_quit();
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
            LOGI("**** INPUT EVENT type: %d\n", AInputEvent_getType(event));
            if(AInputQueue_preDispatchEvent(queue, event) == 0)
            {
                LOGI("**** INPUT EVENT handle\n");
                int handled = 0;
                AInputQueue_finishEvent(queue, event, handled);
            }
        }
    }

    return 1;
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
}
