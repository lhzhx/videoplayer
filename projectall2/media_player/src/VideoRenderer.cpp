#include "VideoRenderer.h"
#include <iostream>
#include <cstring>

// YUV420P着色器源码
static const char* vshader = R"(
#version 330 core
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 tex;
out vec2 TexCoord;
void main() {
    gl_Position = vec4(pos, 0.0, 1.0);
    TexCoord = tex;
}
)";

static const char* fshader = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texY, texU, texV;
void main() {
    float y = texture(texY, TexCoord).r;
    float u = texture(texU, TexCoord).r - 0.5;
    float v = texture(texV, TexCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    FragColor = vec4(r, g, b, 1.0);
}
)";

static float verts[] = {
     1,  1,   1, 0,
     1, -1,   1, 1,
    -1, -1,   0, 1,
    -1,  1,   0, 0
};
static unsigned idx[] = {0, 1, 3, 1, 2, 3};

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint flag;
    glGetShaderiv(s, GL_COMPILE_STATUS, &flag);
    if (!flag) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "Shader error: " << log << std::endl;
        exit(1);
    }
    return s;
}

VideoRenderer::VideoRenderer(int width, int height, const char* title)
    : width_(width), height_(height), window_(nullptr), prog_(0), vao_(0), vbo_(0), ebo_(0), texY_(0), texU_(0), texV_(0), frame_ready_(false) {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        exit(1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window_ = glfwCreateWindow(width_, height_, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        std::cerr << "Failed to create window" << std::endl;
        exit(1);
    }
    glfwMakeContextCurrent(window_);
    glewExperimental = GL_TRUE;
    glewInit();
    initOpenGL();
}

VideoRenderer::~VideoRenderer() {
    cleanup();
    if (window_) {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

void VideoRenderer::initOpenGL() {
    createShaders();
    createTextures();
    createBuffers();
    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "texY"), 0);
    glUniform1i(glGetUniformLocation(prog_, "texU"), 1);
    glUniform1i(glGetUniformLocation(prog_, "texV"), 2);
}

void VideoRenderer::createShaders() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vshader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fshader);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    GLint ok;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog_, 512, nullptr, log);
        std::cerr << "Link error: " << log << std::endl;
        exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
}

void VideoRenderer::createTextures() {
    glGenTextures(1, &texY_); glGenTextures(1, &texU_); glGenTextures(1, &texV_);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, texU_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, texV_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void VideoRenderer::createBuffers() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void VideoRenderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (window_) {
        glfwSetWindowSize(window_, width_, height_);
        glViewport(0, 0, width_, height_);
    }
}

void VideoRenderer::updateFrame(AVFrame* frame) {
    if (!frame) return;
    // 假设frame为YUV420P格式
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width, frame->height, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texU_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width/2, frame->height/2, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texV_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, frame->width/2, frame->height/2, 0, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
    frame_ready_ = true;
}

void VideoRenderer::render() {
    if (!frame_ready_) return;
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glfwSwapBuffers(window_);
}

bool VideoRenderer::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

void VideoRenderer::pollEvents() const {
    glfwPollEvents();
}

GLFWwindow* VideoRenderer::getWindow() const {
    return window_;
}

void VideoRenderer::cleanup() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (texY_) glDeleteTextures(1, &texY_);
    if (texU_) glDeleteTextures(1, &texU_);
    if (texV_) glDeleteTextures(1, &texV_);
} 