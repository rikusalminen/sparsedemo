#include <android/input.h>
#include <android_native_app_glue.h>

static void app_cmd_callback(struct android_app *android_app, int32_t cmd)
{
    (void)android_app;

    switch(cmd)
    {
        case APP_CMD_INPUT_CHANGED:
        case APP_CMD_INIT_WINDOW:
        case APP_CMD_TERM_WINDOW:
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_GAINED_FOCUS:
        case APP_CMD_LOST_FOCUS:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_LOW_MEMORY:
        case APP_CMD_START:
        case APP_CMD_RESUME:
        case APP_CMD_SAVE_STATE:
        case APP_CMD_PAUSE:
        case APP_CMD_STOP:
        case APP_CMD_DESTROY:
        default:
            break;
    }
}

static int32_t input_event_callback(struct android_app* android_app, AInputEvent* event)
{
    (void)android_app;
    (void)event;

    switch(AInputEvent_getType(event))
    {
        case AINPUT_EVENT_TYPE_KEY:
        case AINPUT_EVENT_TYPE_MOTION:
            return 1;
        default:
            break;
    }

    return 0;
}

void android_main(struct android_app *android_app)
{
    (void)android_app;

    // Make sure glue isn't stripped.
    app_dummy();

    android_app->userData = NULL;
    android_app->onAppCmd = app_cmd_callback;
    android_app->onInputEvent = input_event_callback;
}
