#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define WIDTH 640
#define HEIGHT 480

static GLFWwindow *window = NULL;
static GLuint vert = 0;
static GLuint frag = 0;
static GLuint program = 0;
static GLuint vbo = 0;

static void finalize(void) {
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
    }
    if (program != 0) {
        glDeleteProgram(program);
    }
    if (vert != 0) {
        glDeleteShader(vert);
    }
    if (frag != 0) {
        glDeleteShader(frag);
    }
    if (window != NULL) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

static void fatal_error(const char *message) {
    const char *error_description;
    int error_code = glfwGetError(&error_description);
    if (error_code == GLFW_NO_ERROR) {
        fprintf(stderr, "%s\n", message);
    } else {
        fprintf(stderr, "%s: 0x%08x (%s)\n", message, error_code, error_description);
    }
    finalize();
    exit(1);
}

static void init_window(void) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    window = glfwCreateWindow(WIDTH, HEIGHT, "triangle-opengl", NULL, NULL);
    if (window == NULL) {
        fatal_error("Failed to create window");
    }
    glfwMakeContextCurrent(window);
}

static void init_gl(void) {
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fatal_error("Failed to load OpenGL functions");
    }

    printf("Vendor: %s\n", glGetString(GL_VENDOR));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    printf("Version: %s\n", glGetString(GL_VERSION));

    glViewport(0, 0, WIDTH, HEIGHT);
}

static GLuint compile_shader(GLenum shader_type, const char *shader_source_code) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &shader_source_code, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        fputs("Failed to compile shader\n", stderr);
        GLint log_length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        char *log = malloc(log_length);
        if (log == NULL) {
            glDeleteShader(shader);
            fatal_error("Failed to allocate shader log");
        } else {
            glGetShaderInfoLog(shader, log_length, NULL, log);
            glDeleteShader(shader);
            fatal_error(log);
        }
    }
    return shader;
}

static GLuint link_shader_program(GLuint *shaders) {
    GLuint program = glCreateProgram();
    for (; *shaders != 0; ++shaders) {
        glAttachShader(program, *shaders);
    }
    glLinkProgram(program);
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        fputs("Failed to link shader program\n", stderr);
        GLint log_length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        char *log = malloc(log_length);
        if (log == NULL) {
            glDeleteProgram(program);
            fatal_error("Failed to allocate shader program log");
        } else {
            glGetProgramInfoLog(program, log_length, NULL, log);
            glDeleteProgram(program);
            fatal_error(log);
        }
    }
    return program;
}

static void init_shaders(void) {
    vert = compile_shader(GL_VERTEX_SHADER, (
        "attribute vec2 a_position;\n"
        "attribute vec3 a_color;\n"
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_position.xy, 0., 1.);\n"
        "    v_color = a_color;\n"
        "}\n"
    ));
    frag = compile_shader(GL_FRAGMENT_SHADER, (
        "varying vec3 v_color;\n"
        "void main() {\n"
        "    gl_FragColor = vec4(v_color, 1.);\n"
        "}\n"
    ));
}

static void init_program(void) {
    GLuint shaders[] = {vert, frag, 0};
    program = link_shader_program(shaders);

    glDeleteShader(frag);
    frag = 0;
    glDeleteShader(vert);
    vert = 0;

    glUseProgram(program);
}

static void init_vertex_buffer(void) {
    static const float vertex_attributes[] = {
        /* a_position */ /* a_color */
         0.0f,  0.5f,    1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,    0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,    0.0f, 0.0f, 1.0f,
    };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_attributes), vertex_attributes, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

static void save_back_buffer_to_disk(void) {
    uint8_t *buffer = malloc((size_t)4 * (size_t)WIDTH * (size_t)HEIGHT);
    if (buffer == NULL) {
        fatal_error("Failed to allocate buffer");
    }
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    stbi_flip_vertically_on_write(1); /* since OpenGL's coordinate system is flipped vertically */
    puts("Writing screenshot to ./triangle.png");
    int result = stbi_write_png("triangle.png", WIDTH, HEIGHT, 4, buffer, 0);
    free(buffer);
    if (result == 0) {
        fatal_error("Failed to write to output file");
    }
}

static void draw_frame(void) {
    glDrawArrays(GL_TRIANGLES, 0, 3);

    save_back_buffer_to_disk();

    glfwSwapBuffers(window);
}

int main(void) {
    if (glfwInit() != GLFW_TRUE) {
        fatal_error("Failed to initialize GLFW");
    }
    init_window();
    init_gl();
    init_shaders();
    init_program();
    init_vertex_buffer();
    draw_frame();
    finalize();
    return 0;
}
