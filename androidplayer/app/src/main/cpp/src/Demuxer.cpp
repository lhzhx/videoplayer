#include "Demuxer.h"
#include <mutex>
#include <future>
#include <chrono>

// 全局互斥锁，保护FFmpeg格式上下文的线程安全访问
std::mutex avformat_mutex;

extern "C"{
    #include "libavformat/avformat.h"
}
#include <stdexcept>

Demuxer::Demuxer(Packetqueue& videoQueue, Packetqueue& audioQueue) 
    : videoPacketQueue(videoQueue), audioPacketQueue(audioQueue) {}

Demuxer::~Demuxer() {
    stop();  // 停止解复用线程
    std::lock_guard<std::mutex> lock(avformat_mutex);
    if (formatCtx) {
        avformat_close_input(&formatCtx);  // 关闭输入格式上下文
    }
}

bool Demuxer::start(const char* filename) {
    // 检查是否已经在运行
    if (running.load()) return false;

    // 打开输入媒体文件
    if (avformat_open_input(&formatCtx, filename, nullptr, nullptr) != 0) {
        return false;
    }
    std::cout << "Input format: " << formatCtx->iformat->name << std::endl;
    
    // 分析流信息，获取音频和视频流的详细信息
    int afsi = avformat_find_stream_info(formatCtx, nullptr);
    if (afsi < 0) {
        std::cerr << "Failed to find stream info (avformat_find_stream_info)" << std::endl;
        return false;
    }
    
    // 遍历所有流，输出流信息用于调试
    for (int i = 0; i < formatCtx->nb_streams; i++) {
        AVStream* stream = formatCtx->streams[i];
        std::cout << "Stream #" << i << ": type=" << av_get_media_type_string(stream->codecpar->codec_type)
                  << ", codec_id=" << stream->codecpar->codec_id
                  << " (" << avcodec_get_name(stream->codecpar->codec_id) << ")\n";
    }

    // 查找最佳的视频和音频流
    videoStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    
    // 至少需要找到一个音频或视频流
    if (videoStreamIndex < 0 && audioStreamIndex < 0) {
        std::cerr << "No video or audio stream found" << std::endl;
        return false;
    }
    
    std::cout << "Found streams - Video: " << videoStreamIndex << ", Audio: " << audioStreamIndex << std::endl;

    // 启动解复用线程
    running.store(true);
    demuxThread = std::thread(&Demuxer::demuxFunc, this);
    return true;
}

void Demuxer::stop() {
    running.store(false);  // 设置停止标志
    
    // 使用超时机制等待线程结束，避免无限等待
    if (demuxThread.joinable()) {
        auto future = std::async(std::launch::async, [&]() {
            if (demuxThread.joinable()) {
                demuxThread.join();
            }
        });
        
        // 等待500毫秒，如果超时则强制分离线程
        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout) {
            LOGE("Demuxer thread join timeout, forcing detach");
            if (demuxThread.joinable()) {
                demuxThread.detach();
            }
        }
    }
    
    // 清理FFmpeg格式上下文
    std::lock_guard<std::mutex> lock(avformat_mutex);
    if (formatCtx) {
        avformat_close_input(&formatCtx);
    }
}

AVCodecParameters* Demuxer::getVideoCodecParameters() const {
    // 检查视频流是否有效
    if (videoStreamIndex < 0 || !formatCtx || !formatCtx->streams[videoStreamIndex]) {
        return nullptr;
    }
    
    // 分配新的编解码参数结构
    AVCodecParameters* codecPar = avcodec_parameters_alloc();
    if (!codecPar) {
        return nullptr;
    }
    
    // 复制原始流的编解码参数
    if (avcodec_parameters_copy(codecPar, formatCtx->streams[videoStreamIndex]->codecpar) < 0) {
        avcodec_parameters_free(&codecPar);
        return nullptr;
    }
    
    return codecPar;
}

AVCodecParameters* Demuxer::getAudioCodecParameters() const {
    // 检查音频流是否有效
    if (audioStreamIndex < 0 || !formatCtx || !formatCtx->streams[audioStreamIndex]) {
        return nullptr;
    }
    
    // 分配新的编解码参数结构
    AVCodecParameters* codecPar = avcodec_parameters_alloc();
    if (!codecPar) {
        return nullptr;
    }
    
    // 复制原始流的编解码参数
    if (avcodec_parameters_copy(codecPar, formatCtx->streams[audioStreamIndex]->codecpar) < 0) {
        avcodec_parameters_free(&codecPar);
        return nullptr;
    }
    
    return codecPar;
}

void Demuxer::demuxFunc() {
    AVPacket packet;  // 用于存储读取的数据包
    LOGD("Demuxer thread started");
    
    // 主解复用循环，持续运行直到收到停止信号
    while (running.load()) {
        int ret;
        {
            // 使用互斥锁保护FFmpeg格式上下文的访问
            std::lock_guard<std::mutex> lock(avformat_mutex);
            ret = av_read_frame(formatCtx, &packet);
        }
        
        // 处理读取错误或文件结尾
        if (ret < 0) {
            // 文件结尾或暂时无数据，短暂休眠后重试，避免CPU占用过高
            LOGD("av_read_frame returned <0, sleeping and retrying...");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        //检查是否有日志验证是否找到packet
        LOGD("Demuxer got packet, pts=%lld, stream_index=%d", packet.pts, packet.stream_index);
        
        // 根据数据包的流索引，将其分发到相应的队列
        if (packet.stream_index == videoStreamIndex) {
            videoPacketQueue.put(&packet);  // 放入视频队列
        } else if (packet.stream_index == audioStreamIndex) {
            audioPacketQueue.put(&packet);  // 放入音频队列
        } else {
            // 其他流类型的数据包，直接释放
            av_packet_unref(&packet);
        }
        
        //数据包的内存释放由我的线程安全队列实现，不需要额外再实现
    }
    
    LOGD("Demuxer thread exiting");
    running.store(false);  // 确保停止标志被设置
}