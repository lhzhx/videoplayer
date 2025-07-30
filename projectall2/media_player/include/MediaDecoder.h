#pragma once
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "AudioFrameQueue.h"
#include "network_client.h"

class MediaDecoder {
public:
    MediaDecoder();
    ~MediaDecoder();

    // 打开本地文件
    bool open(const std::string& path);
    // 打开自定义网络流
    bool openNetwork(const std::string& ip, int port);
    // 解码一帧，返回true表示有帧，false表示流结束
    bool readFrame();
    // 获取解码后的视频帧（YUV420P），调用者负责释放
    AVFrame* getVideoFrame();
    // 获取解码后的音频帧
    AudioFrame getAudioFrame();
    // 跳转到指定秒数（仅本地文件）
    void seek(double seconds);
    // 刷新解码器缓冲
    void flush();
    // 关闭
    void close();
    // 基本信息
    int width() const;
    int height() const;
    int sampleRate() const;
    int channels() const;
    AVRational videoTimeBase() const;
    AVRational audioTimeBase() const;
    bool isNetworkMode() const;
private:
    // 公共
    std::mutex mtx_;
    int w_, h_;
    int audio_sample_rate_, audio_channels_;
    AVRational time_base_, audio_time_base_;
    std::atomic<bool> quit_;
    // 本地文件
    AVFormatContext* fmt_;
    AVCodecContext* vctx_;
    AVCodecContext* actx_;
    SwsContext* sws_;
    SwrContext* swr_;
    int vstream_, astream_;
    // 网络流
    std::unique_ptr<CTCPClient> net_client_;
    AVCodec* net_vcodec_;
    AVCodec* net_acodec_;
    AVCodecContext* net_vctx_;
    AVCodecContext* net_actx_;
    AVPacket* net_pkt_;
    // 缓存帧
    AVFrame* video_frame_;
    AudioFrame audio_frame_;
    // 网络解码线程
    std::thread net_decode_thread_;
    std::atomic<bool> is_network_mode_;
    // 网络缓冲
    std::vector<AVFrame*> video_queue_;
    std::vector<AudioFrame> audio_queue_;
    void networkDecodeLoop();
}; 