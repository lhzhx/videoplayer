#pragma once
#include <vector>
#include <cstdint>

// 音频输出类
class AudioOutput {
public:
    AudioOutput();
    bool initialize(int sample_rate, int channels);
    void play(const std::vector<float>& samples);
    void close();
    ~AudioOutput();
private:
    bool initialized_;
    int sample_rate_;
    int channels_;
    void* pcm_handle_;
}; 