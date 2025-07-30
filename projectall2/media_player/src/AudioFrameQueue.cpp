#include "AudioFrameQueue.h"

AudioFrame::AudioFrame() : pts(0), sample_rate(0), channels(0) {}
AudioFrame::AudioFrame(const std::vector<float>& s, int64_t p, int sr, int ch)
    : samples(s), pts(p), sample_rate(sr), channels(ch) {}

AudioFrameQueue::AudioFrameQueue(size_t max_size)
    : max_size_(max_size), stopped_(false) {}

void AudioFrameQueue::push(const AudioFrame& frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_full_.wait(lock, [this]() { return queue_.size() < max_size_ || stopped_; });
    if (stopped_) return;
    queue_.push(frame);
    cond_empty_.notify_one();
}

AudioFrame AudioFrameQueue::pop(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms > 0) {
        if (!cond_empty_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                                 [this]() { return !queue_.empty() || stopped_; }))
            return AudioFrame();
    } else {
        cond_empty_.wait(lock, [this]() { return !queue_.empty() || stopped_; });
    }
    if (stopped_ && queue_.empty()) return AudioFrame();
    if (!queue_.empty()) {
        AudioFrame frame = queue_.front();
        queue_.pop();
        cond_full_.notify_one();
        return frame;
    }
    return AudioFrame();
}

void AudioFrameQueue::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cond_empty_.notify_all();
    cond_full_.notify_all();
}

bool AudioFrameQueue::stopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
}

AudioFrameQueue::~AudioFrameQueue() { stop(); } 