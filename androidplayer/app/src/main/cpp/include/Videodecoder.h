//
// Created by 29406 on 2025/7/10.
//

#ifndef ANDROIDPLAYER_VIDEODECODER_H
#define ANDROIDPLAYER_VIDEODECODER_H

#include "Packetqueue.h"
#include "Framequeue.h"
extern "C"{
#include <libavcodec/avcodec.h>
}
#include <atomic>
#include <thread>

class Videodecoder {
public:
    Videodecoder(Packetqueue& packetqueue, Framequeue& framequeue);
    ~Videodecoder();
    bool start(AVCodecParameters* codecPar);
    void stop();
    void flush();

private:
    void VideodecodeThreadFunc();

    AVCodecContext* VideocodecCtx = nullptr;
    Packetqueue& packetqueue;
    Framequeue& framequeue;
    std::thread VideodecodeThread;
    std::atomic<bool> running{false};
};

#endif //ANDROIDPLAYER_VIDEODECODER_H
