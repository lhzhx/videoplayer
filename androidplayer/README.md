# AndroidPlayer Native C++ 层说明

本目录为 AndroidPlayer 项目的 native C++ 层，负责多媒体解复用、音视频解码、渲染、音频播放等高性能底层功能。该层通过 JNI 与 Java/Kotlin 层交互，依赖 FFmpeg、OpenGL ES、AAudio 等库。

## 目录结构

- `src/`：核心 C++ 源文件（音视频解复用、解码、渲染、播放等）
- `include/`：C++ 头文件，声明各模块接口
- `CMakeLists.txt`：CMake 构建脚本，配置依赖与编译参数
- `jniLibs/`：第三方 so 库（如 FFmpeg）

## 主要模块与功能

### 1. Demuxer（解复用器）
负责解析多媒体文件，分离音视频流，推送到对应队列。依赖 FFmpeg 的 AVFormat。

### 2. Videodecoder / AudioDecoder（音视频解码器）
基于 FFmpeg，分别解码视频帧和音频帧，支持多线程异步解码。

### 3. Videorender / ANWRender（视频渲染）
基于 OpenGL ES/EGL 或 ANativeWindow，负责将解码后的视频帧渲染到屏幕，支持水印、纹理等。

### 4. AAudioRender（音频播放）
基于 Android AAudio，低延迟播放 PCM 数据，支持回调、暂停、刷新等。

### 5. AudioRingBuffer（音频环形缓冲区）
线程安全的音频数据缓冲，解码与播放解耦。

### 6. Packetqueue / Framequeue
线程安全的音视频包/帧队列，支持多生产者/消费者。

## 依赖说明

- [FFmpeg](https://ffmpeg.org/)：解复用、解码、重采样等
- OpenGL ES 3.1 / EGL：视频渲染
- Android NDK（AAudio、ANativeWindow、JNI 等）

## 编译与集成

1. **CMake 构建**：
   - `CMakeLists.txt` 配置所有源码、头文件、依赖库（如 FFmpeg so）
   - Gradle 配置 `externalNativeBuild`，自动调用 CMake 编译 native 层
2. **JNI 集成**：
   - Java 层声明 native 方法，通过 `System.loadLibrary` 加载 so
   - C++ 层实现 JNI 接口，完成与 Java 层的数据交互

## 主要类与接口

| 类名             | 主要功能                         |
|------------------|----------------------------------|
| Demuxer          | 媒体解复用，分离音视频流         |
| Videodecoder     | 视频解码，输出 YUV 帧            |
| AudioDecoder     | 音频解码，输出 PCM 数据           |
| Videorender      | OpenGL ES 渲染视频帧             |
| ANWRender        | ANativeWindow 渲染 RGBA 帧        |
| AAudioRender     | 低延迟音频播放                   |
| AudioRingBuffer  | 音频数据缓冲                     |
| Packetqueue      | 音视频包队列                     |
| Framequeue       | 视频帧队列                       |

## JNI 交互示例

**C++ 层 JNI 方法实现**
```cpp
extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidplayer_Player_nativePlay(JNIEnv* env, jobject thiz, jstring path) {
	// ... 调用 Demuxer、Decoder、Render 等 ...
}
```

**Java 层调用**
```java
public class Player {
	static {
		System.loadLibrary("androidplayer");
	}
	public native void nativePlay(String path);
}
```

## 开发与调试建议

- 推荐使用 Android Studio，支持 native 断点、变量查看
- 日志建议用 `__android_log_print` 输出到 Logcat
- C++ 层多线程较多，注意线程安全与资源释放
- 若集成第三方 so（如 FFmpeg），需确保 ABI 匹配

## 常见问题

- so 未生成：检查 CMake 配置、Gradle 配置、NDK 路径
- JNI 方法签名不一致：Java 声明与 C++ 实现需完全一致
- 运行崩溃：检查 so 加载、依赖库、ABI、权限等

## 参考资料

- [Android NDK 官方文档](https://developer.android.com/ndk)
- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [OpenGL ES 官方文档](https://www.khronos.org/opengles/)

---
如需扩展 native 层功能，请参考本目录源码及注释，遵循模块化、线程安全、资源管理等最佳实践。
