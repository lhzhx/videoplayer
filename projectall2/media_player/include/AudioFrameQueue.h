#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>

// 音频帧结构
struct AudioFrame {
    std::vector<float> samples;
    int64_t pts;
    int sample_rate;
    int channels;
    
    AudioFrame();
    AudioFrame(const std::vector<float>& s, int64_t p, int sr, int ch);
};

class AudioFrameQueue {
public:
    AudioFrameQueue(size_t max_size = 50);
    void push(const AudioFrame& frame);
    AudioFrame pop(int timeout_ms = 0);
    void stop();
    bool stopped() const;
    ~AudioFrameQueue();
private:
    std::queue<AudioFrame> queue_;
    size_t max_size_;
    mutable std::mutex mutex_;
    std::condition_variable cond_empty_, cond_full_;
    bool stopped_;
}; 