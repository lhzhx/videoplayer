//
// Created by 29406 on 2025/7/11.
//

#ifndef ANDROIDPLAYER_VIDEORENDER_H
#define ANDROIDPLAYER_VIDEORENDER_H
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include "android/native_window.h"
extern "C"{
#include <libavutil/frame.h>
}

class Videorender {
public:
    Videorender(ANativeWindow* nativeWindow);
    ~Videorender();

    //初始化绘制表面，EGl, 着色器， 纹理，以及上下文
    bool initialize(ANativeWindow* nativeWindow);
    void renderFrame(AVFrame* frame);
    
    // 设置水印图片
    bool setWatermark(const char* imagePath);

private:
    bool initShaders();
    void initTextures();
    void updateTexturesFromAVFrame(AVFrame* frame);
    bool loadWatermarkTexture(const char* imagePath);
    ANativeWindow* m_nativeWindow;

    // EGL相关成员
    EGLDisplay m_display;
    EGLContext m_context;
    EGLSurface m_surface;

    // OpenGL ES相关成员
    GLuint m_program;
    GLuint m_textureY;
    GLuint m_textureU;
    GLuint m_textureV;
    
    // 水印相关成员
    GLuint m_watermarkTexture;
    bool m_hasWatermark;
    int m_watermarkWidth;
    int m_watermarkHeight;
};
#endif //ANDROIDPLAYER_VIDEORENDER_H
