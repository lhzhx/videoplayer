#include "AudioDecoder.h"
extern "C"{
    #include "libavcodec/avcodec.h"    // FFmpeg音频解码库
    #include "libavutil/opt.h"         // FFmpeg选项设置
    #include "libswresample/swresample.h" // FFmpeg音频重采样库
}
#include <iostream>  // 标准输入输出流
#include <future>    // 异步操作支持
#include <chrono>    // 时间处理


AudioDecoder::AudioDecoder(Packetqueue& packetqueue, AudioRingBuffer& ringBuffer) : packetqueue(packetqueue), ringBuffer(ringBuffer) {}


AudioDecoder::~AudioDecoder() {
    stop();  // 停止解码线程
    
    // 释放音频解码器上下文
    if (audioCodecCtx) {
        avcodec_free_context(&audioCodecCtx);
    }
    
    // 释放重采样器上下文
    if (swrCtx) {
        swr_free(&swrCtx);
    }
}

bool AudioDecoder::start(AVCodecParameters* codecPar) {
    // 1. 基础检查 - 确保解码器未运行且参数有效
    if (running.load()) {
        std::cerr << "Audio decoder already running" << std::endl;
        return false;
    }
    
    // 打印音频编解码参数信息，用于调试
    std::cout << "Audio codec parameters:\n"
          << "  Codec ID: " << codecPar->codec_id << " (" << avcodec_get_name(codecPar->codec_id) << ")\n"
          << "  Sample Rate: " << codecPar->sample_rate << "\n"
          << "  Channels: " << codecPar->ch_layout.nb_channels << "\n"
          << "  Sample Format: " << codecPar->format << "\n"
          << "  Bitrate: " << codecPar->bit_rate << std::endl;
    
    // 验证编解码参数的有效性
    if (!codecPar || codecPar->codec_type != AVMEDIA_TYPE_AUDIO) {
        std::cerr << "Invalid audio codec parameters" << std::endl;
        return false;
    }

    // 2. 查找解码器 - 根据编解码ID查找对应的解码器
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        std::cerr << "Audio codec not found: " << codecPar->codec_id << std::endl;
        return false;
    }

    // 3. 创建解码器上下文 - 分配解码器上下文内存
    audioCodecCtx = avcodec_alloc_context3(codec);
    if (!audioCodecCtx) {
        std::cerr << "Failed to allocate audio codec context" << std::endl;
        return false;
    }

    // 4. 复制参数到解码器上下文 - 将媒体参数复制到解码器上下文
    if (avcodec_parameters_to_context(audioCodecCtx, codecPar) < 0) {
        std::cerr << "Failed to copy audio codec parameters" << std::endl;
        avcodec_free_context(&audioCodecCtx);
        return false;
    }

    // 5. 打开解码器 - 初始化解码器，准备开始解码
    if (avcodec_open2(audioCodecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open audio codec" << std::endl;
        avcodec_free_context(&audioCodecCtx);
        return false;
    }

    // 6. 初始化重采样器 - 用于音频格式转换
    swrCtx = swr_alloc();
    if (!swrCtx) {
        std::cerr << "Failed to allocate SwrContext" << std::endl;
        avcodec_free_context(&audioCodecCtx);
        return false;
    }

    // 设置重采样输入参数（从解码器获取）
    av_opt_set_chlayout(swrCtx, "in_chlayout", &audioCodecCtx->ch_layout, 0);  // 输入声道布局
    av_opt_set_int(swrCtx, "in_sample_rate", audioCodecCtx->sample_rate, 0);   // 输入采样率
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", audioCodecCtx->sample_fmt, 0); // 输入采样格式
    
    // 设置重采样输出参数（目标格式）
    AVChannelLayout targetLayout = AV_CHANNEL_LAYOUT_STEREO;  // 目标立体声布局
    av_opt_set_chlayout(swrCtx, "out_chlayout", &targetLayout, 0);     // 输出声道布局
    av_opt_set_int(swrCtx, "out_sample_rate", targetSampleRate, 0);    // 输出采样率
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", targetFormat, 0);  // 输出采样格式

    // 初始化重采样器上下文
    if (swr_init(swrCtx) < 0) {
        std::cerr << "Failed to initialize SwrContext" << std::endl;
        swr_free(&swrCtx);
        avcodec_free_context(&audioCodecCtx);
        return false;
    }

    // 7. 启动解码线程 - 开始异步音频解码处理
    running.store(true);  // 设置运行标志
    audioDecodeThread = std::thread(&AudioDecoder::AudioDecodeThreadFunc, this);
    
    std::cout << "Audio decoder started successfully" << std::endl;
    return true;
}

void AudioDecoder::stop() {
    running.store(false);  // 设置停止标志，通知解码线程退出
    
    // 使用超时机制等待线程结束，避免主线程阻塞
    if (audioDecodeThread.joinable()) {
        // 使用异步操作在另一个线程内处理线程结束任务，防止本线程阻塞
        auto future = std::async(std::launch::async, [&]() {
            if (audioDecodeThread.joinable()) {
                audioDecodeThread.join();  // 等待解码线程正常结束
            }
        });
        
        // 等待3秒，如果超时则认为线程已结束或无响应
        if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
            std::cerr << "Audio decode thread did not stop within timeout, detaching..." << std::endl;
            audioDecodeThread.detach();  // 强制分离线程，避免资源泄漏
        }
    }
    
    std::cout << "Audio decoder stopped" << std::endl;
}

void AudioDecoder::flush() {
    // 创建并发送flush packet到解码器
    // 空数据包会触发解码器清空内部缓冲区
    AVPacket* pkt = av_packet_alloc();
    pkt->data = nullptr;  // 空数据指针表示flush操作
    pkt->size = 0;        // 数据大小为0
    packetqueue.put(pkt); // 将flush包放入队列，解码线程会处理
}

void AudioDecoder::AudioDecodeThreadFunc() {
    // 分配音频帧结构，用于接收解码后的数据
    AVFrame* frame = av_frame_alloc();
    if (!frame) return;  // 分配失败，直接退出

    // 分配重采样输出缓冲区，用于存储格式转换后的音频数据
    uint8_t* outputBuffer = nullptr;
    int outputBufferSize = 0;

    // 主解码循环，持续运行直到收到停止信号
    while (running.load()) {
        // 从数据包队列获取待解码的音频包
        AVPacket* packet = packetqueue.get();
        if (!packet) {
            // 超时或队列为空，检查是否应该停止
            if (!running.load()) {
                break;
            }
            continue;  // 继续等待新的数据包
        }

        // 将数据包发送到解码器进行解码
        int ret = avcodec_send_packet(audioCodecCtx, packet);
        av_packet_free(&packet);  // 释放数据包内存
        
        if (ret < 0) {
            std::cerr << "Error sending audio packet to decoder" << std::endl;
            continue;  // 发送失败，处理下一个包
        }

        // 从解码器接收解码后的音频帧
        // 一个数据包可能产生多个音频帧
        while (ret >= 0 && running.load()) {
            ret = avcodec_receive_frame(audioCodecCtx, frame);
            
            // 处理特殊返回值
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;  // 需要更多输入数据或到达文件末尾
            }
            if (ret < 0) {
                std::cerr << "Error receiving audio frame from decoder" << std::endl;
                break;  // 解码错误，跳出内层循环
            }

            // 计算重采样后的输出样本数和缓冲区大小
            int outputSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
            int outputLineSize = outputSamples * targetChannels * av_get_bytes_per_sample(targetFormat);
            
            // 动态调整输出缓冲区大小
            if (outputLineSize > outputBufferSize) {
                av_freep(&outputBuffer);  // 释放旧缓冲区
                outputBuffer = (uint8_t*)av_malloc(outputLineSize);  // 分配新缓冲区
                outputBufferSize = outputLineSize;
                if (!outputBuffer) {
                    std::cerr << "Failed to allocate output buffer" << std::endl;
                    break;  // 内存分配失败，退出
                }
            }

            // 执行音频重采样，将解码后的音频转换为目标格式
            int convertedSamples = swr_convert(swrCtx, &outputBuffer, outputSamples, 
                                             (const uint8_t**)frame->data, frame->nb_samples);
            
            if (convertedSamples < 0) {
                std::cerr << "Error converting audio samples" << std::endl;
                continue;  // 重采样失败，处理下一帧
            }

            // 计算实际输出的数据大小
            int actualOutputSize = convertedSamples * targetChannels * av_get_bytes_per_sample(targetFormat);
            
            // 根据播放速度调整音频数据
            float currentSpeed = playbackSpeed.load();
            if (actualOutputSize > 0) {
                if (currentSpeed == 1.0f) {
                    // 正常速度，直接写入
                    size_t written = ringBuffer.write(outputBuffer, actualOutputSize, std::chrono::milliseconds(100));
                    if (written != actualOutputSize) {
                        std::cerr << "Warning: Only wrote " << written << " of " << actualOutputSize 
                                 << " bytes to ring buffer" << std::endl;
                    }
                } else if (currentSpeed > 1.0f) {
                    // 快速播放：跳过部分样本
                    int skipRatio = static_cast<int>(currentSpeed);
                    int bytesPerSample = targetChannels * av_get_bytes_per_sample(targetFormat);
                    int totalSamples = actualOutputSize / bytesPerSample;
                    int outputSampleCount = totalSamples / skipRatio;
                    int adjustedOutputSize = outputSampleCount * bytesPerSample;
                    
                    // 创建跳帧后的缓冲区
                    uint8_t* speedAdjustedBuffer = (uint8_t*)av_malloc(adjustedOutputSize);
                    if (speedAdjustedBuffer) {
                        for (int i = 0; i < outputSampleCount; i++) {
                            memcpy(speedAdjustedBuffer + i * bytesPerSample, 
                                   outputBuffer + (i * skipRatio) * bytesPerSample, 
                                   bytesPerSample);
                        }
                        
                        size_t written = ringBuffer.write(speedAdjustedBuffer, adjustedOutputSize, std::chrono::milliseconds(100));
                        if (written != adjustedOutputSize) {
                            std::cerr << "Warning: Only wrote " << written << " of " << adjustedOutputSize 
                                     << " bytes to ring buffer (speed adjusted)" << std::endl;
                        }
                        av_freep(&speedAdjustedBuffer);
                    }
                } else {
                    // 慢速播放：重复样本
                    int repeatRatio = static_cast<int>(1.0f / currentSpeed);
                    int bytesPerSample = targetChannels * av_get_bytes_per_sample(targetFormat);
                    int totalSamples = actualOutputSize / bytesPerSample;
                    int adjustedOutputSize = totalSamples * repeatRatio * bytesPerSample;
                    
                    // 创建重复样本后的缓冲区
                    uint8_t* speedAdjustedBuffer = (uint8_t*)av_malloc(adjustedOutputSize);
                    if (speedAdjustedBuffer) {
                        for (int i = 0; i < totalSamples; i++) {
                            for (int j = 0; j < repeatRatio; j++) {
                                memcpy(speedAdjustedBuffer + (i * repeatRatio + j) * bytesPerSample,
                                       outputBuffer + i * bytesPerSample,
                                       bytesPerSample);
                            }
                        }
                        
                        size_t written = ringBuffer.write(speedAdjustedBuffer, adjustedOutputSize, std::chrono::milliseconds(100));
                        if (written != adjustedOutputSize) {
                            std::cerr << "Warning: Only wrote " << written << " of " << adjustedOutputSize 
                                     << " bytes to ring buffer (speed adjusted)" << std::endl;
                        }
                        av_freep(&speedAdjustedBuffer);
                    }
                }
            }

            // 释放帧引用，准备接收下一帧
            av_frame_unref(frame);
        }
    }

    // 线程结束前的清理工作
    av_frame_free(&frame);        // 释放音频帧结构
    av_freep(&outputBuffer);      // 释放输出缓冲区
    
    std::cout << "Audio decode thread finished" << std::endl;
}

//设置音频的速度，根据按钮按下的反馈
void AudioDecoder::setSpeed(float speed) {
    // 验证速度值
    if (speed <= 0.0f || speed > 4.0f) {
        std::cerr << "Invalid speed value: " << speed << ". Must be between 0.1 and 4.0" << std::endl;
        return;
    }
    
    playbackSpeed.store(speed);
    std::cout << "Audio playback speed set to: " << speed << "x" << std::endl;
}