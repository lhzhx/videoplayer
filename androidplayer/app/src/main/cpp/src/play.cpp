#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <cmath>
#include "Demuxer.h"
#include "Videodecoder.h"
#include "Videorender.h"
#include "AudioDecoder.h"
#include "AudioRingBuffer.h"
#include "AAudioRender.h"
#include "Packetqueue.h"
#include "Framequeue.h"
#include <mutex>
#include "AAudioRender.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavformat/avformat.h>
}

#define LOG_TAG "NativePlayer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
static std::mutex seek_mutex;
extern std::mutex avformat_mutex;
static std::mutex decoder_mutex;
static std::thread renderthread;
static std::atomic<bool> shouldStop(false);
static std::atomic<bool> isPaused(false);
static std::atomic<int64_t> current_pts(0);
static Packetqueue* videoPacketQueue = nullptr;
static Packetqueue* audioPacketQueue = nullptr;
static Framequeue* videoFrameQueue = nullptr;
static AudioRingBuffer* audioRingBuffer = nullptr;
static Demuxer* demuxer = nullptr;
static Videodecoder* videodecoder = nullptr;
static AudioDecoder* audioDecoder = nullptr;
static Videorender* videorender = nullptr;
static AAudioRender* audioRender = nullptr;
static std::atomic<bool> isSeeking(false);
static std::atomic<int64_t> last_valid_pts(0);
static std::atomic<float> playback_speed(1.0f);

// 音频回调函数
aaudio_data_callback_result_t audioCallback(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {
    //增加对于回调时候，音频的回调函数的判断是否处于暂停状态
    if (!audioRingBuffer || shouldStop.load() || isPaused.load()) {
        // 如果没有音频缓冲区、停止状态或暂停状态，输出静音
        memset(audioData, 0, numFrames * 2 * sizeof(int16_t)); // 2通道，16位
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    
    size_t bytesToRead = numFrames * 2 * sizeof(int16_t); // 2通道，16位
    size_t bytesRead = audioRingBuffer->tryRead((uint8_t*)audioData, bytesToRead);
    
    // 如果读取的数据不足，用静音填充剩余部分
    if (bytesRead < bytesToRead) {
        memset((uint8_t*)audioData + bytesRead, 0, bytesToRead - bytesRead);
    }
    
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

// 播放线程
void renderLoop(ANativeWindow* nativeWindow) {
    videorender = new Videorender(nativeWindow);
    
    // 启用水印显示
    const char* watermarkPath = "/sdcard/a/watermark.jpg";
    if (videorender->setWatermark(watermarkPath)) {
        LOGD("Custom watermark loaded successfully from: %s", watermarkPath);
    } else {
        LOGE("Failed to load watermark from: %s, using test watermark", watermarkPath);
        //自己的照片没找到就显示一个红色的
        if (videorender->setWatermark("test")) {
            LOGD("Test watermark enabled successfully");
        } else {
            LOGE("Failed to enable any watermark");
        }
    }
    
    const int base_frame_delay_ms = 42;
    int retry_count = 0;
    //设置尝试获取帧的次数，超过一百次还没有就退出
    const int max_retry = 100;
    
    while(!shouldStop.load()) {
        // 检查暂停和seeking状态
        if (isPaused.load() || isSeeking.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            retry_count = 0; // 重置重试计数
            continue;
        }
        
        // 尝试获取帧，但不要无限等待
        auto frame = videoFrameQueue->get();
        if(frame == nullptr) {
            retry_count++;
            if (retry_count >= max_retry) {
                LOGD("Max retry reached, checking if should stop");
                if (shouldStop.load()) {
                    break;
                }
                retry_count = 0; // 重置计数继续尝试
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        retry_count = 0; // 成功获取帧，重置计数
        
        // 检查帧的有效性，避免渲染seek后的无效帧
        if (frame->pts <= 0) {
            // 释放无效帧
            av_frame_free(&frame);
            continue;
        }
        
        // 如果刚完成seek操作，确保第一个渲染的帧是关键帧
        // 这样可以避免因为非关键帧缺少参考帧而导致的花屏问题
        static bool need_keyframe = false;
        static std::atomic<bool> last_seeking_state(false);
        
        // 检测seek状态变化
        bool current_seeking = isSeeking.load();
        if (last_seeking_state.load() && !current_seeking) {
            // seek刚刚完成，需要等待关键帧
            need_keyframe = true;
            LOGD("Seek completed, waiting for keyframe");
        }
        last_seeking_state = current_seeking;
        
        // 如果需要关键帧，检查当前帧是否为关键帧
        if (need_keyframe) {
            bool is_keyframe = (frame->flags & AV_FRAME_FLAG_KEY) || 
                              (frame->pict_type == AV_PICTURE_TYPE_I);
            
            if (!is_keyframe) {
                // 如果不是关键帧，跳过这一帧
                LOGD("Skipping non-keyframe after seek, pict_type=%d, flags=0x%x", 
                     frame->pict_type, frame->flags);
                av_frame_free(&frame);
                continue;
            } else {
                // 找到关键帧，重置标志
                need_keyframe = false;
                LOGD("Found keyframe after seek, pict_type=%d, flags=0x%x", 
                     frame->pict_type, frame->flags);
            }
        }
        
        // 更新PTS
        current_pts = frame->pts;
        last_valid_pts = frame->pts;
        
        // 渲染帧
        auto start = std::chrono::steady_clock::now();
        videorender->renderFrame(frame);
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        // 释放帧资源
        av_frame_free(&frame);
        
        // 帧率控制，根据播放速度动态调整
        float current_speed = playback_speed.load();
        int frame_delay_ms = static_cast<int>(base_frame_delay_ms / current_speed);
        if (elapsed < frame_delay_ms && !shouldStop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(frame_delay_ms - elapsed));
        }
    }
     
    ANativeWindow_release(nativeWindow);
    LOGD("render thread exit");
    // 不要在这里delete videorender
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativePlay(JNIEnv *env, jobject thiz, jstring file, jobject surface) {
    LOGD("nativePlay called");

    // 停止并清理之前的线程和对象
    shouldStop = true;
    
    // 使用超时机制等待之前的线程结束
    if(renderthread.joinable()) {
        // flush队列以唤醒可能阻塞的线程
        if (videoFrameQueue) {
            videoFrameQueue->flush();
        }
        if (videoPacketQueue) {
            videoPacketQueue->flush();
        }
        if (audioPacketQueue) {
            audioPacketQueue->flush();
        }

        
        auto future = std::async(std::launch::async, [&]() {
            if (renderthread.joinable()) {
                renderthread.join();
            }
        });
        
        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout) {
            LOGE("Previous thread join timeout in nativePlay, forcing detach");
            if (renderthread.joinable()) {
                renderthread.detach();
            }
        }
    }
    shouldStop = false;
    isPaused = false;
    current_pts = 0;
    last_valid_pts = 0;
    isSeeking = false;
    playback_speed = 1.0f;
    if (demuxer) { delete demuxer; demuxer = nullptr; }
    if (videodecoder) { delete videodecoder; videodecoder = nullptr; }
    if (audioDecoder) { delete audioDecoder; audioDecoder = nullptr; }
    if (videorender) { delete videorender; videorender = nullptr; }
    if (audioRender) { delete audioRender; audioRender = nullptr; }
    if (videoPacketQueue) { delete videoPacketQueue; videoPacketQueue = nullptr; }
    if (audioPacketQueue) { delete audioPacketQueue; audioPacketQueue = nullptr; }
    if (videoFrameQueue) { delete videoFrameQueue; videoFrameQueue = nullptr; }
    if (audioRingBuffer) { delete audioRingBuffer; audioRingBuffer = nullptr; }

    // 创建全局对象
    videoPacketQueue = new Packetqueue();
    audioPacketQueue = new Packetqueue();
    videoFrameQueue = new Framequeue();
    audioRingBuffer = new AudioRingBuffer(1024 * 1024); // 1MB音频缓冲区
    demuxer = new Demuxer(*videoPacketQueue, *audioPacketQueue);
    const char* c_file = env->GetStringUTFChars(file, nullptr);
    if (!demuxer->start(c_file)) {
        LOGE("demuxer start failed");
        env->ReleaseStringUTFChars(file, c_file);
        return -1;
    }
    videodecoder = new Videodecoder(*videoPacketQueue, *videoFrameQueue);
    AVCodecParameters* videoCodecPar = demuxer->getVideoCodecParameters();
    if (!videoCodecPar || !videodecoder->start(videoCodecPar)) {
        LOGE("video decoder start failed");
        env->ReleaseStringUTFChars(file, c_file);
        return -1;
    }

    // 启动音频解码器
    audioDecoder = new AudioDecoder(*audioPacketQueue, *audioRingBuffer);
    AVCodecParameters* audioCodecPar = demuxer->getAudioCodecParameters();
    if (audioCodecPar && demuxer->getAudioStreamIndex() >= 0) {
        if (!audioDecoder->start(audioCodecPar)) {
            LOGE("audio decoder start failed, continuing without audio");
            delete audioDecoder;
            audioDecoder = nullptr;
        } else {
            LOGD("audio decoder started successfully");
            
            // 初始化并启动AAudioRender
            audioRender = new AAudioRender();
            audioRender->configure(44100, 2, AAUDIO_FORMAT_PCM_I16);
            audioRender->setCallback(audioCallback, nullptr);
            
            if (audioRender->start() == 0) {
                LOGD("Audio render started successfully");
            } else {
                LOGE("Failed to start audio render");
                delete audioRender;
                audioRender = nullptr;
            }
        }
    } else {
        LOGD("no audio stream found, continuing video only");
        delete audioDecoder;
        audioDecoder = nullptr;
    }

    ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
    if (!nativeWindow) {
        LOGE("nativeWindow is null");
        env->ReleaseStringUTFChars(file, c_file);
        return -1;
    }

    renderthread = std::thread(renderLoop, nativeWindow);

    env->ReleaseStringUTFChars(file, c_file);
    LOGD("nativePlay finished");
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidplayer_Player_nativePause(JNIEnv *env, jobject thiz, jboolean p) {
    LOGD("nativePause called, pause=%d", p);
    //更改全局变量，控制所有线程的开始和启动
    isPaused = p;
    

}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeStop(JNIEnv *env, jobject thiz) {
    LOGD("nativeStop called");
    
    // 设置停止标志
    shouldStop = true;
    isSeeking = false;

    // 停止组件，释放可能的阻塞
    if (demuxer) {
        demuxer->stop();
    }
    if (videodecoder) {
        videodecoder->stop();
    }
    if (audioDecoder) {
        audioDecoder->stop();
    }
    if (audioRingBuffer) {
        audioRingBuffer->stop();
    }

    // flush队列，唤醒所有阻塞的线程
    if (videoPacketQueue) {
        videoPacketQueue->flush();
    }
    if (audioPacketQueue) {
        audioPacketQueue->flush();
    }
    if (videoFrameQueue) {
        videoFrameQueue->flush();
    }

    // 等待渲染线程结束，使用超时机制避免死锁
    if (renderthread.joinable()) {
        // 使用future和async实现超时join
        auto future = std::async(std::launch::async, [&]() {
            if (renderthread.joinable()) {
                renderthread.join();
            }
        });
        
        // 等待最多500ms
        if (future.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout) {
            LOGE("Thread join timeout, forcing detach");
            if (renderthread.joinable()) {
                renderthread.detach();
            }
        }
    }

    // 重置状态变量
    current_pts = 0;
    last_valid_pts = 0;

    // 清理资源
    if (videorender) {
        delete videorender;
        videorender = nullptr;
    }
    if (demuxer) {
        delete demuxer;
        demuxer = nullptr;
    }
    if (videodecoder) {
        delete videodecoder;
        videodecoder = nullptr;
    }
    if (audioDecoder) {
        delete audioDecoder;
        audioDecoder = nullptr;
    }
    if (audioRender) {
        delete audioRender;
        audioRender = nullptr;
    }
    if (videoPacketQueue) {
        delete videoPacketQueue;
        videoPacketQueue = nullptr;
    }
    if (audioPacketQueue) {
        delete audioPacketQueue;
        audioPacketQueue = nullptr;
    }
    if (videoFrameQueue) {
        delete videoFrameQueue;
        videoFrameQueue = nullptr;
    }
    if (audioRingBuffer) {
        delete audioRingBuffer;
        audioRingBuffer = nullptr;
    }
    
    LOGD("nativeStop finished");
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeSeek(JNIEnv *env, jobject thiz, jdouble position) {
    LOGD("nativeSeek called, pos=%f", position);
    if (!demuxer || !demuxer->getFormatContext()) return -1;
    AVFormatContext* fmt = demuxer->getFormatContext();

    // 优先用 AVFormatContext 的 duration
    int64_t total_duration = fmt->duration; // 单位 AV_TIME_BASE
    if (total_duration <= 0) {
        // fallback: 用第一个视频流的 duration
        int streamIdx = demuxer->getVideoStreamIndex();
        if (streamIdx >= 0 && streamIdx < fmt->nb_streams && fmt->streams) {
            AVStream* stream = fmt->streams[streamIdx];
            if (stream && stream->duration > 0) {
                total_duration = av_rescale_q(stream->duration, stream->time_base, {1, AV_TIME_BASE});
            }
        }
    }
    if (total_duration <= 0) {
        LOGE("duration invalid, cannot seek");
        return -1;
    }

    auto seek_target = (int64_t)(position * total_duration);
    if (seek_target < 0) seek_target = 0;
    if (seek_target > total_duration) seek_target = total_duration;

    // 设置seeking标志，让渲染线程暂停
    isSeeking = true;
    
    // 等待一小段时间确保渲染线程已经暂停
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    {
        //设置最小粒度锁，以防死锁的情况
        std::lock_guard<std::mutex> lock(avformat_mutex);
        // 移除AVSEEK_FLAG_ANY标志，确保只seek到关键帧，避免花屏问题
        int ret = av_seek_frame(fmt, -1, seek_target, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            LOGE("av_seek_frame failed");
            isSeeking = false;
            return -1;
        }
        avformat_flush(fmt);
    }
    
    // 清理所有缓冲区
    if (videoPacketQueue) videoPacketQueue->flush();
    if (audioPacketQueue) audioPacketQueue->flush();
    if (videoFrameQueue) videoFrameQueue->flush();
    if (audioRingBuffer) audioRingBuffer->flush();
    
    {
        std::lock_guard<std::mutex> lock(decoder_mutex);
        if (videodecoder) videodecoder->flush();
        if (audioDecoder) audioDecoder->flush();
    }

    // 更新当前位置到seek目标位置
    int streamIdx = demuxer->getVideoStreamIndex();
    if (streamIdx >= 0 && streamIdx < fmt->nb_streams && fmt->streams) {
        AVStream* stream = fmt->streams[streamIdx];
        if (stream && stream->time_base.den > 0) {
            // 将seek_target转换为stream的time_base
            int64_t stream_pts = av_rescale_q(seek_target, {1, AV_TIME_BASE}, stream->time_base);
            current_pts = stream_pts;
            last_valid_pts = stream_pts;
        }
    }
    
    // 等待一小段时间让解码器产生新的帧
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    isSeeking = false;
    LOGD("nativeSeek finished, target_pos=%f", position);
    
    // 额外等待确保渲染线程能够检测到seek完成状态
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeSetSpeed(JNIEnv *env, jobject thiz, jfloat speed) {
    LOGD("nativeSetSpeed called, speed=%f", speed);
    
    // 处理Java层传入的int值（会被自动转换为float）
    // 对于接近整数的值，进行四舍五入处理
    float normalized_speed = speed;
    if (abs(speed - 1.0f) < 0.01f) normalized_speed = 1.0f;
    else if (abs(speed - 2.0f) < 0.01f) normalized_speed = 2.0f;
    else if (abs(speed - 3.0f) < 0.01f) normalized_speed = 3.0f;
    else if (abs(speed - 0.5f) < 0.01f) normalized_speed = 0.5f;
    
    // 验证倍速值，只支持0.5、1.0、2.0、3.0倍速
    if (normalized_speed != 0.5f && normalized_speed != 1.0f && normalized_speed != 2.0f && normalized_speed != 3.0f) {
        LOGE("Unsupported speed: %f. Only 0.5, 1.0, 2.0, 3.0 are supported.", speed);
        return -1;
    }
    
    // 设置视频播放倍速
    playback_speed.store(normalized_speed);
    
    // 设置音频播放倍速
    if (audioDecoder) {
        audioDecoder->setSpeed(normalized_speed);
        LOGD("Audio speed set to: %fx", normalized_speed);
    }
    
    LOGD("Playback speed set to: %fx", normalized_speed);
    
    return 0;
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_example_androidplayer_Player_nativeGetPosition(JNIEnv *env, jobject thiz) {
    if (!demuxer || !demuxer->getFormatContext()) return 0.0;

    // 如果正在seeking，返回上一个有效位置
    if (isSeeking.load()) {
        AVFormatContext* fmt = demuxer->getFormatContext();
        int streamIdx = demuxer->getVideoStreamIndex();
        if (streamIdx >= 0 && streamIdx < fmt->nb_streams && fmt->streams && last_valid_pts > 0) {
            AVStream* stream = fmt->streams[streamIdx];
            if (stream && stream->time_base.den > 0) {
                return (double)last_valid_pts * stream->time_base.num / stream->time_base.den;
            }
        }
        return 0.0;
    }

    AVFormatContext* fmt = demuxer->getFormatContext();
    int streamIdx = demuxer->getVideoStreamIndex();
    if (streamIdx < 0 || streamIdx >= fmt->nb_streams || !fmt->streams) return 0.0;

    AVStream* stream = fmt->streams[streamIdx];
    if (!stream) return 0.0;

    int64_t pts = current_pts.load();

    // 如果当前pts无效，使用last_valid_pts
    if (pts <= 0) {
        pts = last_valid_pts.load();
    } else {
        // 更新last_valid_pts
        last_valid_pts = pts;
    }

    if (pts <= 0) return 0.0;

    // 确保time_base有效
    if (stream->time_base.den <= 0) return 0.0;

    double pos = (double)pts * stream->time_base.num / stream->time_base.den;

    // 确保返回值在合理范围内
    if (pos < 0.0) pos = 0.0;

    return pos;
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_example_androidplayer_Player_nativeGetDuration(JNIEnv *env, jobject thiz) {
    LOGD("nativeGetDuration called");
    if (!demuxer || !demuxer->getFormatContext()) {
        LOGD("demuxer or format context is null");
        return 0.0;
    }

    AVFormatContext* fmt = demuxer->getFormatContext();

    // 优先使用AVFormatContext的duration
    if (fmt->duration > 0) {
        double duration = (double)fmt->duration / AV_TIME_BASE;
        LOGD("duration from format context: %f", duration);
        return duration;
    }

    // 如果format context的duration无效，尝试使用视频流的duration
    int streamIdx = demuxer->getVideoStreamIndex();
    if (streamIdx >= 0 && streamIdx < fmt->nb_streams && fmt->streams) {
        AVStream* stream = fmt->streams[streamIdx];
        if (stream && stream->duration > 0 && stream->time_base.den > 0) {
            // 将流的duration转换为秒
            double duration = (double)stream->duration * stream->time_base.num / stream->time_base.den;
            LOGD("duration from video stream: %f", duration);
            return duration;
        }
    }

    // 如果都无效，尝试估算duration
    if (fmt->bit_rate > 0 && fmt->pb && fmt->pb->pos > 0) {
        // 基于文件大小和比特率估算
        int64_t file_size = avio_size(fmt->pb);
        if (file_size > 0) {
            double estimated_duration = (double)file_size * 8.0 / fmt->bit_rate;
            LOGD("estimated duration: %f", estimated_duration);
            return estimated_duration;
        }
    }

    LOGD("unable to determine duration");
    return 0.0;
}