#include <android_native_app_glue.h>

void android_main(struct android_app *android_app)
{
    (void)android_app;

    // Make sure glue isn't stripped.
    app_dummy();
}
