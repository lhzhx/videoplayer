#include "FrameQueue.h"
extern "C" {
#include <libavutil/frame.h>
}

FrameQueue::FrameQueue(size_t max_size) : max_size_(max_size), stopped_(false) {}

void FrameQueue::push(AVFrame* frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_full_.wait(lock, [this]() { return queue_.size() < max_size_ || stopped_; });
    if (stopped_) {
        av_frame_free(&frame);
        return;
    }
    queue_.push(frame);
    cond_empty_.notify_one();
}

AVFrame* FrameQueue::pop(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms > 0) {
        if (!cond_empty_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return !queue_.empty() || stopped_; }))
            return nullptr;
    } else {
        cond_empty_.wait(lock, [this]() { return !queue_.empty() || stopped_; });
    }
    if (stopped_ && queue_.empty()) return nullptr;
    if (!queue_.empty()) {
        AVFrame* frame = queue_.front();
        queue_.pop();
        cond_full_.notify_one();
        return frame;
    }
    return nullptr;
}

void FrameQueue::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    while (!queue_.empty()) { av_frame_free(&queue_.front()); queue_.pop(); }
    cond_empty_.notify_all();
    cond_full_.notify_all();
}

void FrameQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        av_frame_free(&queue_.front());
        queue_.pop();
    }
    cond_full_.notify_all();
    cond_empty_.notify_all();
}

bool FrameQueue::stopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
}

FrameQueue::~FrameQueue() { stop(); clear(); } 