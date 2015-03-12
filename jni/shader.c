#include <GLXW/glxw.h>

#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, __FILE__, __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, __FILE__, __VA_ARGS__))

static GLuint compile_shader(GLenum shader_type, const char *src)
{
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(status != GL_TRUE)
    {
        GLint size;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);

        char buffer[size+1];
        GLsizei len;
        glGetShaderInfoLog(shader, size, &len, buffer);
        buffer[len] = 0;

        LOGW("Shader compiler error:\n%s\n", buffer);

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint link_program(const GLuint *shaders, int num_shaders)
{
    GLuint prog = glCreateProgram();

    for(int i = 0; i < num_shaders; ++i)
        if(shaders[i])
            glAttachShader(prog, shaders[i]);

    glLinkProgram(prog);

    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if(status != GL_TRUE)
    {
        GLint size;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);

        char buffer[size+1];
        GLsizei len;
        glGetProgramInfoLog(prog, size, &len, buffer);
        buffer[len] = 0;

        LOGW("Program linking error:\n%s\n", buffer);

        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

unsigned shader_compile(const char *vert, const char *tess_ctrl, const char *tess_eval, const char *geom, const char *frag)
{
    GLenum types[] = {
        GL_VERTEX_SHADER,
        GL_TESS_CONTROL_SHADER,
        GL_TESS_EVALUATION_SHADER,
        GL_GEOMETRY_SHADER,
        GL_FRAGMENT_SHADER
    };
    const int num_shaders = sizeof(types)/sizeof(*types);
    const char *srcs[] = { vert, tess_ctrl, tess_eval, geom, frag };
    GLuint shaders[num_shaders];
    GLuint prog = 0;

    for(int i = 0; i < num_shaders; ++i)
    {
        shaders[i] = 0;

        if(!srcs[i] || !srcs[i][0]) continue;
        if(!(shaders[i] = compile_shader(types[i], srcs[i])))
            goto finish;
    }

    prog = link_program(shaders, num_shaders);

finish:
    for(int i = 0; i < num_shaders; ++i)
    {
        if(shaders[i])
            glDeleteShader(shaders[i]);
    }

    return prog;
}
