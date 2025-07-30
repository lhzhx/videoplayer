//
// Created for AudioPlayer
//

#ifndef ANDROIDPLAYER_AUDIODECODER_H
#define ANDROIDPLAYER_AUDIODECODER_H

#include "Packetqueue.h"
#include "AudioRingBuffer.h"
extern "C"{
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
#include <atomic>
#include <thread>

class AudioDecoder {
public:
    AudioDecoder(Packetqueue& packetqueue, AudioRingBuffer& ringBuffer);
    ~AudioDecoder();
    bool start(AVCodecParameters* codecPar);
    void stop();
    void flush();
    void setSpeed(float speed);  // 设置播放速度

private:
    void AudioDecodeThreadFunc();

    AVCodecContext* audioCodecCtx = nullptr;
    SwrContext* swrCtx = nullptr;
    Packetqueue& packetqueue;
    AudioRingBuffer& ringBuffer;
    std::thread audioDecodeThread;
    std::atomic<bool> running{false};
    
    // 音频参数
    int targetSampleRate = 44100;
    int targetChannels = 2;
    AVSampleFormat targetFormat = AV_SAMPLE_FMT_S16;
    
    // 播放速度控制
    std::atomic<float> playbackSpeed{1.0f};  // 播放速度倍率
};

#endif //ANDROIDPLAYER_AUDIODECODER_H