#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstddef>
extern "C" {
#include <libavutil/frame.h>
}

// 线程安全帧队列，支持stop和notify_all，防止卡死
class FrameQueue {
public:
    FrameQueue(size_t max_size = 10);
    void push(AVFrame* frame);
    AVFrame* pop(int timeout_ms = 0);
    void stop();
    void clear();
    bool stopped() const;
    ~FrameQueue();
private:
    std::queue<AVFrame*> queue_;
    size_t max_size_;
    mutable std::mutex mutex_;
    std::condition_variable cond_empty_, cond_full_;
    bool stopped_;
};
