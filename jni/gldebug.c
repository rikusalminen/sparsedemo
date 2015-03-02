#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

static const char* gl_debug_source_string(GLenum source)
{
    switch(source)
    {
        case GL_DEBUG_SOURCE_API_ARB: return "API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB: return "WINDOW_SYSTEM";
        case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB: return "SHADER_COMPILER";
        case GL_DEBUG_SOURCE_THIRD_PARTY_ARB: return "THIRD_PARTY";
        case GL_DEBUG_SOURCE_APPLICATION_ARB: return "APPLICATION";
        case GL_DEBUG_SOURCE_OTHER_ARB: return "OTHER";
        default: break;
    }

    return "";
}

static const char* gl_debug_type_string(GLenum type)
{
    switch(type)
    {
        case GL_DEBUG_TYPE_ERROR_ARB: return "ERROR";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: return "DEPRECATED_BEHAVIOR";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB: return "UNDEFINED_BEHAVIOR";
        case GL_DEBUG_TYPE_PORTABILITY_ARB: return "PORTABILITY";
        case GL_DEBUG_TYPE_PERFORMANCE_ARB: return "PERFORMANCE";
        case GL_DEBUG_TYPE_OTHER_ARB: return "OTHER";
        default: break;
    }

    return "";
}

static const char* gl_debug_severity_string(GLenum severity)
{
    switch(severity)
    {
        case GL_DEBUG_SEVERITY_HIGH_ARB: return "HIGH";
        case GL_DEBUG_SEVERITY_MEDIUM_ARB: return "MEDIUM";
        case GL_DEBUG_SEVERITY_LOW_ARB: return "LOW";
        default: break;
    }

    return "";
}

void APIENTRY gl_debug_callback(
    GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    (void)length; (void)userParam;

    LOGW("GL DEBUG  source: %s  type: %s  id: %x  severity: %s  message: \"%s\"\n",
            gl_debug_source_string(source),
            gl_debug_type_string(type),
            id,
            gl_debug_severity_string(severity),
            message);
}
