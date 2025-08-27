# 视频播放器项目合集

本仓库包含两个主要项目，分别面向 Android 平台和跨平台 C++，实现了本地视频播放、音视频解码、OpenGL 渲染、网络流媒体播放及 TCP 服务端等功能。

---

## 1. androidplayer

### 项目简介
`androidplayer` 是一个基于 Android 平台的视频播放器应用，支持本地视频文件的播放、音视频解码、OpenGL 渲染等功能，适合移动端多媒体播放场景。

### 主要功能
- 支持本地视频文件播放
- 音视频同步解码与播放
- 使用 OpenGL 进行高效视频渲染
- 支持水印叠加
- 结构化的多模块代码，便于维护和扩展

### 技术实现
- **语言/平台**：Kotlin、C++（JNI）、Android
- **音视频解码**：JNI 调用 C++ 实现底层解码
- **渲染**：OpenGL ES
- **架构**：模块化设计，包含详细的流程图（见 `architecture_flowchart.svg`、`audio_decoder_flowchart.svg`、`opengl_rendering_flowchart.svg`）

### 快速开始
1. 使用 Android Studio 打开 `androidplayer` 项目
2. 配置好 Android NDK 和 CMake 环境
3. 编译并运行到 Android 设备

---

## 2. projectall2

### 项目简介
`projectall2` 是一个基于 C++ 的跨平台音视频流媒体播放器，包含本地播放器和基于 TCP 的网络流媒体服务端，适合桌面端和服务器端音视频处理与传输场景。

### 主要功能
- 本地音视频文件解码与播放
- 支持音视频帧队列、同步与渲染
- 基于 TCP 的音视频流实时传输
- 提供高性能的 epoll 网络服务端示例

### 技术实现
- **语言/平台**：C++
- **模块划分**：
  - `media_player/`：本地播放器核心，包含音视频解码、帧队列、渲染等模块
  - `tcpepollserver/`：基于 epoll 的高性能 TCP 服务端，支持音视频流传输
- **主要文件**：
  - `include/`：头文件，定义各模块接口
  - `src/`：核心实现，包括 `main.cpp`、`MediaDecoder.cpp`、`network_client.cpp` 等
  - `test_server_epoll.cpp`：TCP 服务端测试代码

### 快速开始
1. 进入 `projectall2/media_player` 目录，使用 CMake 构建项目
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ./media_player
   ```
2. 进入 `tcpepollserver` 目录，编译并运行 TCP 服务端
   ```bash
   g++ test_server_epoll.cpp -o test_server_epoll
   ./test_server_epoll
   ```

---

## 适用场景

- 移动端本地视频播放与处理
- 桌面端音视频流媒体播放
- 局域网/互联网音视频实时传输
- 多媒体教学、远程会议、流媒体服务器开发

---

## 目录结构说明

- `androidplayer/`：Android 视频播放器项目
- `projectall2/`：C++ 音视频播放器与 TCP 服务端项目
  - `media_player/`：本地播放器核心
  - `tcpepollserver/`：TCP 网络服务端

---

如需详细依赖和使用说明，请参考各项目内的 `README.md` 或源码注释。
