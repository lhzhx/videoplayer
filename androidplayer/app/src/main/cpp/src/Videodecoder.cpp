#include "Videodecoder.h"
extern "C"{
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
}
#include <iostream>
#include <future>
#include <chrono>

Videodecoder::Videodecoder(Packetqueue& packetqueue, Framequeue& framequeue)
    : packetqueue(packetqueue), framequeue(framequeue) {}

Videodecoder::~Videodecoder() {
    stop();  // 停止解码线程
    if (VideocodecCtx) {
        avcodec_free_context(&VideocodecCtx);  // 释放解码上下文
    }
}

bool Videodecoder::start(AVCodecParameters* codecPar) {
    // 1. 基础状态检查
    if (running.load()) {
        std::cerr << "Decoder already running" << std::endl;
        return false;
    }
    
    // 输出视频编解码参数信息用于调试
    std::cout << "Video codec parameters:\n"
          << "  Codec ID: " << codecPar->codec_id << " (" << avcodec_get_name(codecPar->codec_id) << ")\n"
          << "  Width: " << codecPar->width << "\n"
          << "  Height: " << codecPar->height << "\n"
          << "  Pixel Format: " << codecPar->format << " (" << av_get_pix_fmt_name((AVPixelFormat)codecPar->format) << ")\n"
          << "  Bitrate: " << codecPar->bit_rate << std::endl;

    // 验证编解码参数的有效性
    if (!codecPar || codecPar->codec_type != AVMEDIA_TYPE_VIDEO) {
        std::cerr << "Invalid video codec parameters" << std::endl;
        return false;
    }

    // 2. 输出初始化信息
    std::cout << "Initializing decoder for:\n"
              << "  Codec ID: " << codecPar->codec_id << "\n"
              << "  Width: " << codecPar->width << "\n"
              << "  Height: " << codecPar->height << "\n"
              << "  Format: " << codecPar->format << std::endl;

    // 3. 查找对应的视频解码器
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        std::cerr << "Unsupported codec: " << avcodec_get_name(codecPar->codec_id) << std::endl;
        return false;
    }

    // 4. 分配解码器上下文
    VideocodecCtx = avcodec_alloc_context3(codec);
    if (!VideocodecCtx) {
        std::cerr << "Failed to allocate codec context" << std::endl;
        return false;
    }

    // 5. 将编解码参数复制到解码器上下文
    if (avcodec_parameters_to_context(VideocodecCtx, codecPar) < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        avcodec_free_context(&VideocodecCtx);
        return false;
    }

    // 6. 打开解码器，准备开始解码
    if (avcodec_open2(VideocodecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        avcodec_free_context(&VideocodecCtx);
        return false;
    }

    // 7. 启动解码线程开始工作
    running.store(true);
    VideodecodeThread = std::thread(&Videodecoder::VideodecodeThreadFunc, this);
    return true;
}

void Videodecoder::stop() {
    running.store(false);  // 设置停止标志
    
    // 使用超时机制等待线程结束，避免无限等待
    if (VideodecodeThread.joinable()) {
        auto future = std::async(std::launch::async, [&]() {
            if (VideodecodeThread.joinable()) {
                VideodecodeThread.join();
            }
        });
        
        // 等待500毫秒，如果超时则强制分离线程
        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout) {
            std::cerr << "Videodecoder thread join timeout, forcing detach" << std::endl;
            if (VideodecodeThread.joinable()) {
                VideodecodeThread.detach();
            }
        }
    }
}

void Videodecoder::flush() {
    // 创建并发送空数据包到解码器进行刷新
    AVPacket* pkt = av_packet_alloc();
    pkt->data = nullptr;  // 空数据表示刷新信号
    pkt->size = 0;
    packetqueue.put(pkt);
}

void Videodecoder::VideodecodeThreadFunc() {
    // 分配视频帧结构，用于接收解码后的数据
    AVFrame* frame = av_frame_alloc();
    if (!frame) return;  // 分配失败，直接退出

    // 主解码循环，持续运行直到收到停止信号
    while (running.load()) {
        // 从数据包队列获取待解码的视频包
        AVPacket* packet = packetqueue.get();
        if (!packet) {
            // 超时或队列为空，检查是否应该停止
            if (!running.load()) {
                break;
            }
            continue;  // 继续等待新的数据包
        }

        // 将数据包发送到解码器进行解码
        int ret = avcodec_send_packet(VideocodecCtx, packet);
        av_packet_free(&packet);  // 释放数据包内存
        if (ret < 0) continue;    // 发送失败，处理下一个包

        // 从解码器接收解码后的视频帧
        // 一个数据包可能产生多个视频帧
        while (ret >= 0 && running.load()) {
            ret = avcodec_receive_frame(VideocodecCtx, frame);
            
            // 处理特殊返回值
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;  // 需要更多输入数据或到达文件末尾
            } else if (ret < 0) {
                break;  // 解码错误，跳出内层循环
            }

            // 将解码后的视频帧放入帧队列，供渲染器使用
            framequeue.put(frame);
            av_frame_unref(frame);  // 释放帧引用，准备接收下一帧
        }
    }
    
    // 线程结束前的清理工作
    av_frame_free(&frame);    // 释放视频帧结构
    running.store(false);     // 确保停止标志被设置
}