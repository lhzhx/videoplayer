#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <libavutil/frame.h>

class VideoRenderer {
public:
    VideoRenderer(int width, int height, const char* title = "Video Player");
    ~VideoRenderer();

    void resize(int width, int height);
    void updateFrame(AVFrame* frame); // 更新YUV420P帧到纹理
    void render(); // 执行绘制
    bool shouldClose() const;
    void pollEvents() const;
    GLFWwindow* getWindow() const;

private:
    void initOpenGL();
    void createShaders();
    void createTextures();
    void createBuffers();
    void cleanup();

    int width_, height_;
    GLFWwindow* window_;
    GLuint prog_;
    GLuint vao_, vbo_, ebo_;
    GLuint texY_, texU_, texV_;
    bool frame_ready_;
}; 