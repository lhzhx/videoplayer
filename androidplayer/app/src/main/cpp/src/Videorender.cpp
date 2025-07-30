#include "Videorender.h"
#include <GLES3/gl31.h>
#include <EGL/egl.h>
#include "android/native_window.h"
#include <iostream>
#include <android/bitmap.h>
#include <android/asset_manager.h>
#include <fstream>
#include <vector>
#include <cstring>

// 集成stb_image库用于图片加载
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
Videorender::Videorender(ANativeWindow* nativeWindow) {
    m_nativeWindow = nativeWindow;
    // 初始化水印相关成员变量
    m_watermarkTexture = 0;
    m_hasWatermark = false;
    m_watermarkWidth = 0;
    m_watermarkHeight = 0;
    // 初始化EGL和OpenGL ES上下文
    initialize(nativeWindow);
}


Videorender::~Videorender() {
    // 清理OpenGL着色器程序
    if (m_program) {
        glDeleteProgram(m_program);
    }
    
    // 清理YUV纹理对象
    if (m_textureY) {
        glDeleteTextures(1, &m_textureY);
    }
    if (m_textureU) {
        glDeleteTextures(1, &m_textureU);
    }
    if (m_textureV) {
        glDeleteTextures(1, &m_textureV);
    }
    
    // 清理水印纹理对象
    if (m_watermarkTexture) {
        glDeleteTextures(1, &m_watermarkTexture);
    }
    
    // 终止EGL显示连接
    eglTerminate(m_display);
}

bool Videorender::initialize(ANativeWindow* nativeWindow) {
    // 获取EGL默认显示连接
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get EGL display" << std::endl;
        return false;
    }

    // 初始化EGL显示连接
    if (!eglInitialize(m_display, nullptr, nullptr)) {
        std::cerr << "Failed to initialize EGL" << std::endl;
        return false;
    }

    // 配置EGL属性：支持OpenGL ES 3.0，窗口表面，8位RGB颜色
    const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,  // 支持OpenGL ES 3.0
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,         // 窗口表面类型
            EGL_BLUE_SIZE, 8,                        // 蓝色通道8位
            EGL_GREEN_SIZE, 8,                       // 绿色通道8位
            EGL_RED_SIZE, 8,                         // 红色通道8位
            EGL_NONE                                 // 配置结束标志
    };

    // 选择匹配的EGL配置
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(m_display, configAttribs, &config, 1, &numConfigs)) {
        std::cerr << "Failed to choose EGL config" << std::endl;
        return false;
    }

    // 设置OpenGL ES上下文属性（版本3.0）
    const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,  // 
            EGL_NONE                        // 属性结束标志
    };

    // 创建OpenGL ES上下文
    m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, contextAttribs);
    if (m_context == EGL_NO_CONTEXT) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return false;
    }

    // 创建窗口表面并设置为当前上下文
    m_surface = eglCreateWindowSurface(m_display, config, nativeWindow, nullptr);
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        std::cerr << "Failed to make EGL context current" << std::endl;
        return false;
    }

    // 初始化OpenGL着色器程序
    if (!initShaders()) {
        std::cerr << "Failed to initialize shaders" << std::endl;
        return false;
    }

    // 初始化YUV纹理对象
    initTextures();

    return true;
}

bool Videorender::initShaders() {
    // 顶点着色器源码：处理顶点位置和纹理坐标
    const char* vertexShaderSource = R"(
        #version 310 es
        precision highp float;
        layout(location = 0) in vec2 aPosition;  // 顶点位置属性
        layout(location = 1) in vec2 aTexCoord;  // 纹理坐标属性
        out vec2 vTexCoord;                      // 输出纹理坐标给片段着色器
        void main() {
            gl_Position = vec4(aPosition, 0.0, 1.0);  // 设置顶点位置
            vTexCoord = aTexCoord;                    // 传递纹理坐标
        }
    )";

    // 片段着色器源码：YUV420P到RGB颜色空间转换并支持水印叠加
    const char* fragmentShaderSource = R"(
        #version 310 es
        precision highp float;
        in vec2 vTexCoord;              // 从顶点着色器接收的纹理坐标
        out vec4 FragColor;             // 输出的像素颜色
        uniform sampler2D uTextureY;    // Y分量纹理（亮度）
        uniform sampler2D uTextureU;    // U分量纹理（色度）
        uniform sampler2D uTextureV;    // V分量纹理（色度）
        uniform sampler2D uWatermark;   // 水印纹理
        uniform bool uHasWatermark;     // 是否有水印
        uniform vec2 uWatermarkPos;     // 水印位置（归一化坐标）
        uniform vec2 uWatermarkSize;    // 水印大小（归一化坐标）
        void main() {
            // 从YUV纹理中采样各分量值
            float y = texture(uTextureY, vTexCoord).r;
            float u = texture(uTextureU, vTexCoord).r - 0.5;  // U分量偏移
            float v = texture(uTextureV, vTexCoord).r - 0.5;  // V分量偏移

            // YUV到RGB的标准转换公式
            float r = y + 1.402 * v;
            float g = y - 0.344 * u - 0.714 * v;
            float b = y + 1.772 * u;
            
            vec4 videoColor = vec4(r, g, b, 1.0);  // 视频颜色
            
            //如果有水印，进行水印叠加
            if (uHasWatermark) {
                // 计算当前像素在水印区域的相对坐标
                vec2 watermarkCoord = (vTexCoord - uWatermarkPos) / uWatermarkSize;
                
                // 检查是否在水印区域内
                if (watermarkCoord.x >= 0.0 && watermarkCoord.x <= 1.0 && watermarkCoord.y >= 0.0 && watermarkCoord.y <= 1.0) {
                    // 采样水印纹理
                    vec4 watermarkColor = texture(uWatermark, watermarkCoord);
                    
                    // 使用alpha混合函数
                    FragColor = mix(videoColor, watermarkColor, watermarkColor.a);
                } else {
                    FragColor = videoColor;
                }
            } else {
                FragColor = videoColor;
            }
        }
    )";

    // 创建并编译顶点着色器
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    // 创建并编译片段着色器
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    // 创建着色器程序并链接着色器
    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);    // 附加顶点着色器
    glAttachShader(m_program, fragmentShader);  // 附加片段着色器
    glLinkProgram(m_program);                   // 链接程序

    // 检查程序链接是否成功
    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(m_program, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
        return false;
    }

    // 删除单独的着色器对象（已链接到程序中）
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return true;
}

void Videorender::initTextures() {
    // 生成三个纹理对象，分别用于Y、U、V分量
    glGenTextures(1, &m_textureY);  // Y分量纹理（亮度）
    glGenTextures(1, &m_textureU);  // U分量纹理（色度）
    glGenTextures(1, &m_textureV);  // V分量纹理（色度）

    // 为每个纹理配置相同的采样参数
    for (auto texture : {m_textureY, m_textureU, m_textureV}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        // 设置纹理过滤方式为线性插值，提供平滑的缩放效果
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // 设置纹理包装方式为边缘夹紧，避免边界采样问题
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}


void Videorender::renderFrame(AVFrame* frame) {
    // 检查输入帧的有效性
    if (!frame || !frame->data[0]) {
        std::cerr << "Invalid AVFrame!" << std::endl;
        return;
    }

    // 1. 将AVFrame的YUV数据更新到OpenGL纹理
    updateTexturesFromAVFrame(frame);

    // 2. 获取EGL表面（窗口）的实际尺寸
    EGLint winWidth, winHeight;
    eglQuerySurface(m_display, m_surface, EGL_WIDTH, &winWidth);
    eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &winHeight);

    // 获取视频帧的原始尺寸
    int videoWidth = frame->width;
    int videoHeight = frame->height;

    // 3. 计算缩放比例，保持视频原始宽高比
    float scaleX = (float)winWidth / videoWidth;   // 水平缩放比例
    float scaleY = (float)winHeight / videoHeight; // 垂直缩放比例
    float scale = std::min(scaleX, scaleY);        // 选择较小的比例以保持宽高比

    // 计算在窗口中的实际绘制尺寸（归一化坐标）
    float drawW = videoWidth * scale / winWidth;
    float drawH = videoHeight * scale / winHeight;

    // 4. 计算居中显示的顶点坐标
    float x = drawW;
    float y = drawH;
    GLfloat vertices[] = {
            -x, -y,  // 左下角
            x, -y,  // 右下角
            -x,  y,  // 左上角
            x,  y   // 右上角
    };

    //纹理坐标，纹理坐标和绘制时的图形坐标不一样，（0，0）在左上角
    static const GLfloat texCoords[] = {
            0.0f, 1.0f,  // 左下角对应纹理底部
            1.0f, 1.0f,  // 右下角对应纹理底部
            0.0f, 0.0f,  // 左上角对应纹理顶部
            1.0f, 0.0f   // 右上角对应纹理顶部
    };

    // 5. 清除颜色缓冲区，准备新的帧渲染
    glClear(GL_COLOR_BUFFER_BIT);

    // 6. 激活我们的着色器程序
    glUseProgram(m_program);

    // 7. 绑定YUV纹理到不同的纹理单元
    // 绑定Y分量纹理到纹理单元0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureY);
    glUniform1i(glGetUniformLocation(m_program, "uTextureY"), 0);

    // 绑定U分量纹理到纹理单元1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textureU);
    glUniform1i(glGetUniformLocation(m_program, "uTextureU"), 1);

    // 绑定V分量纹理到纹理单元2
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_textureV);
    glUniform1i(glGetUniformLocation(m_program, "uTextureV"), 2);
    
    // 设置水印相关uniform变量
    glUniform1i(glGetUniformLocation(m_program, "uHasWatermark"), m_hasWatermark ? 1 : 0);
    
    if (m_hasWatermark && m_watermarkTexture) {
        // 绑定水印纹理到纹理单元3
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_watermarkTexture);
        glUniform1i(glGetUniformLocation(m_program, "uWatermark"), 3);
        
        // 计算水印在右下角的位置和大小（归一化坐标）
        float watermarkSizeX = 0.2f;  // 水印宽度占屏幕20%
        float watermarkSizeY = 0.2f;  // 水印高度占屏幕20%
        float watermarkPosX = 1.0f - watermarkSizeX;  // 右下角X位置
        float watermarkPosY = 1.0f - watermarkSizeY;  // 右下角Y位置
        
        glUniform2f(glGetUniformLocation(m_program, "uWatermarkPos"), watermarkPosX, watermarkPosY);
        glUniform2f(glGetUniformLocation(m_program, "uWatermarkSize"), watermarkSizeX, watermarkSizeY);
    }

    // 8. 设置顶点属性数组
    // 设置顶点位置属性（location = 0）
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    // 设置纹理坐标属性（location = 1）
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texCoords);
    glEnableVertexAttribArray(1);

    // 绘制四边形（使用三角形条带）
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // 9. 交换前后缓冲区，显示渲染结果
    eglSwapBuffers(m_display, m_surface);
}

void Videorender::updateTexturesFromAVFrame(AVFrame* frame) {
    // 更新Y分量纹理（亮度信息，全分辨率）
    glBindTexture(GL_TEXTURE_2D, m_textureY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                 frame->width, frame->height, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[0]);

    // 更新U分量纹理（色度信息，1/4分辨率）
    glBindTexture(GL_TEXTURE_2D, m_textureU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                 frame->width / 2, frame->height / 2, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[1]);

    // 更新V分量纹理（色度信息，1/4分辨率）
    glBindTexture(GL_TEXTURE_2D, m_textureV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                 frame->width / 2, frame->height / 2, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, frame->data[2]);
}

//设置水印
bool Videorender::setWatermark(const char* imagePath) {
    if (!imagePath) {
        return false;
    }
    // 加载水印纹理
    if (loadWatermarkTexture(imagePath)) {
        m_hasWatermark = true;
        return true;
    } else {
        m_hasWatermark = false;
        return false;
    }
}

bool Videorender::loadWatermarkTexture(const char* imagePath) {
    std::cout << "Loading watermark from: " << imagePath << std::endl;

    // 使用stb_image加载图片
    int width, height, channels;
    
    //加载为RGBA格式
    unsigned char* imageData = stbi_load(imagePath, &width, &height, &channels, 4);
    
    if (!imageData) {
        std::cerr << "Failed to load watermark image: " << imagePath << std::endl;
        std::cerr << "stb_image error: " << stbi_failure_reason() << std::endl;
        return false;
    }
    
    std::cout << "Image loaded successfully: " << width << "x" << height << " channels: " << channels << std::endl;
    
    // 创建OpenGL纹理
    glGenTextures(1, &m_watermarkTexture);
    glBindTexture(GL_TEXTURE_2D, m_watermarkTexture);
    
    // 设置纹理参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // 上传纹理数据（stbi_load强制转换为RGBA，所以总是4通道）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData);
    
    // 释放图片数据内存
    stbi_image_free(imageData);
    
    m_watermarkWidth = width;
    m_watermarkHeight = height;
    
    std::cout << "Watermark texture created successfully from image: " << width << "x" << height << std::endl;
    return true;
}