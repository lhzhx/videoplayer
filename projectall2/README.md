# 多媒体播放器项目

一个基于C++开发的高性能音视频播放器系统，支持本地文件播放和网络流媒体传输。

## 🎯 项目特性

### 核心功能
- **多源播放支持**：支持本地视频文件播放和网络流媒体播放
- **音视频同步**：实现精确的音视频同步播放
- **交互式控制**：支持播放/暂停、快进/快退等基本播放控制
- **高性能渲染**：基于OpenGL的硬件加速视频渲染
- **低延迟音频**：使用ALSA实现低延迟音频输出

### 技术亮点
- **多线程架构**：采用生产者-消费者模式，音视频解码和渲染分离
- **网络流媒体**：自定义TCP协议实现实时流媒体传输
- **高效I/O**：服务端使用epoll实现高并发连接处理
- **内存管理**：智能指针和RAII模式确保资源安全

## 🏗️ 系统架构

### 客户端组件

#### 核心模块
- **MediaDecoder**：媒体解码器，支持本地文件和网络流解码
- **VideoRenderer**：OpenGL视频渲染器，处理YUV420P格式转换和显示
- **AudioOutput**：ALSA音频输出管理
- **NetworkClient**：TCP网络客户端，处理流媒体数据接收

#### 数据管理
- **FrameQueue**：线程安全的视频帧队列
- **AudioFrameQueue**：音频帧缓冲队列

### 服务端组件
- **TCP Epoll Server**：高性能流媒体服务器
  - 基于epoll的事件驱动架构
  - 支持多客户端并发连接
  - 实时视频流分发
  - 自定义数据包协议

## 🛠️ 技术栈

### 多媒体处理
- **FFmpeg**：音视频编解码、格式转换
- **libavformat/libavcodec**：媒体容器和编解码器
- **libswscale/libswresample**：图像和音频重采样

### 图形渲染
- **OpenGL**：硬件加速图形渲染
- **GLFW**：跨平台窗口管理
- **GLEW**：OpenGL扩展加载

### 音频处理
- **ALSA**：Linux音频子系统接口

### 网络通信
- **TCP Socket**：可靠的网络数据传输
- **epoll**：高效的I/O事件通知机制

### 构建系统
- **CMake**：跨平台构建配置
- **pkg-config**：依赖库管理

## 📋 系统要求

### 依赖库
- FFmpeg (libavformat, libavcodec, libswscale, libswresample, libavutil)
- OpenGL
- GLFW3
- GLEW
- ALSA
- libjpeg
- libpng
- pthread

### 编译环境
- CMake 3.10+
- C++17 支持的编译器
- pkg-config

## 🚀 编译和安装

### 1. 安装依赖

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install cmake build-essential pkg-config
sudo apt-get install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev
sudo apt-get install libgl1-mesa-dev libglfw3-dev libglew-dev
sudo apt-get install libasound2-dev libjpeg-dev libpng-dev
```

#### CentOS/RHEL
```bash
sudo yum install cmake gcc-c++ pkgconfig
sudo yum install ffmpeg-devel
sudo yum install mesa-libGL-devel glfw-devel glew-devel
sudo yum install alsa-lib-devel libjpeg-turbo-devel libpng-devel
```

### 2. 编译项目

```bash
# 进入项目目录
cd projectall2/media_player

# 创建构建目录
mkdir -p build
cd build

# 配置和编译
cmake ..
make

# 编译服务器
cd ../../tcpepollserver
g++ -o test_server_epoll test_server_epoll.cpp -lavformat -lavcodec -lavutil
```

## 🎮 使用方法

### 本地文件播放
```bash
./media_player video_file.mp4
```

### 网络流播放

#### 1. 启动流媒体服务器
```bash
./test_server_epoll <port> <video_file>
# 例如：./test_server_epoll 8080 sample.mp4
```

#### 2. 连接播放
```bash
./media_player --network <server_ip> <port>
# 例如：./media_player --network 127.0.0.1 8080
```

### 播放控制
- **空格键**：播放/暂停
- **左箭头**：快退5秒（仅本地文件）
- **右箭头**：快进5秒（仅本地文件）
- **Q键**：退出播放器

## 📁 项目结构

```
projectall2/
├── README.md                 # 项目说明文档
├── VID_20230311_211931.mp4   # 示例视频文件
├── media_player/             # 客户端播放器
│   ├── CMakeLists.txt        # 构建配置
│   ├── build/                # 构建输出目录
│   │   └── media_player      # 可执行文件
│   ├── include/              # 头文件
│   │   ├── AudioFrameQueue.h
│   │   ├── AudioOutput.h
│   │   ├── FrameQueue.h
│   │   ├── MediaDecoder.h
│   │   ├── VideoRenderer.h
│   │   └── network_client.h
│   └── src/                  # 源代码
│       ├── AudioFrameQueue.cpp
│       ├── AudioOutput.cpp
│       ├── FrameQueue.cpp
│       ├── MediaDecoder.cpp
│       ├── VideoRenderer.cpp
│       ├── main.cpp
│       └── network_client.cpp
└── tcpepollserver/           # 流媒体服务器
    ├── test_server_epoll.cpp # 服务器源码
    ├── test_server_epoll     # 服务器可执行文件
    └── video_server          # 备用服务器
```

## 🔧 配置说明

### 网络协议
项目使用自定义的TCP数据包协议：

```cpp
struct PacketHeader {
    uint32_t magic;      // 魔数校验 (0x12345678)
    uint32_t dataType;   // 数据类型 (0: 视频, 1: 音频, 2: 元数据)
    uint32_t dataSize;   // 负载数据大小
    int64_t  pts;        // 帧时间戳
};
```

### 音视频参数
- **视频格式**：YUV420P
- **音频格式**：浮点PCM
- **同步机制**：基于PTS时间戳

## 🎯 应用场景

- **本地媒体播放**：支持常见视频格式的本地播放
- **流媒体服务**：构建简单的视频点播系统
- **实时传输**：低延迟的音视频流传输
- **教育演示**：多媒体编程学习和演示
- **原型开发**：快速验证音视频处理算法

## 🐛 故障排除

### 常见问题

1. **编译错误：找不到FFmpeg库**
   ```bash
   # 确保安装了开发包
   sudo apt-get install libavformat-dev libavcodec-dev
   ```

2. **运行时错误：无法打开音频设备**
   ```bash
   # 检查ALSA配置
   aplay -l
   ```

3. **网络连接失败**
   - 检查防火墙设置
   - 确认服务器端口未被占用
   - 验证网络连通性

4. **视频无法显示**
   - 确认OpenGL驱动正常
   - 检查视频格式支持

## 📄 许可证

本项目仅供学习和研究使用。使用的第三方库请遵循各自的许可证条款。

## 🤝 贡献

欢迎提交Issue和Pull Request来改进项目。

## 📞 联系方式

如有问题或建议，请通过GitHub Issues联系。

---

**注意**：本项目主要在Linux环境下开发和测试，Windows支持可能需要额外配置。