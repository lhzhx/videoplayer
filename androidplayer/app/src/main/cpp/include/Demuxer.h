//
// Created by 29406 on 2025/7/10.
//

#ifndef ANDROIDPLAYER_DEMUXER_H
#define ANDROIDPLAYER_DEMUXER_H


#include "Packetqueue.h"
extern "C"{
#include <libavformat/avformat.h>
}
#include <atomic>
#include <thread>
#include <mutex>
extern std::mutex avformat_mutex;
class Demuxer {
public:
    Demuxer(Packetqueue& videoQueue, Packetqueue& audioQueue);
    ~Demuxer();

    bool start(const char* filename);
    void stop();
    //获取video索引，预留后序解码音频
    int getVideoStreamIndex() const { return videoStreamIndex; }
    int getAudioStreamIndex() const { return audioStreamIndex; }
    AVCodecParameters* getVideoCodecParameters() const;
    AVCodecParameters* getAudioCodecParameters() const;
    AVFormatContext* getFormatContext() const { return formatCtx; }

private:
    void demuxFunc();

    AVFormatContext* formatCtx = nullptr;
    Packetqueue& videoPacketQueue;
    Packetqueue& audioPacketQueue;
    std::thread demuxThread;
    std::atomic<bool> running{false};
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
};

#endif //ANDROIDPLAYER_DEMUXER_H
