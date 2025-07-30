//
// Created by 29406 on 2025/7/10.
//

#ifndef ANDROIDPLAYER_PACKETQUEUE_H
#define ANDROIDPLAYER_PACKETQUEUE_H

#include "threadsafequeue.h"
extern "C"{
#include <libavcodec/avcodec.h>
}
class Packetqueue : public threadsafequeue<AVPacket*> {
public:
    void put(AVPacket* pkt) {
        AVPacket* packet = av_packet_alloc();
        av_packet_ref(packet, pkt);
        push(packet);
    }

    AVPacket* get() {
        AVPacket* pkt = nullptr;
        if (wait_and_pop(pkt, std::chrono::milliseconds(100))) {
            return pkt;
        }
        return nullptr;
    }

    void flush() {
        AVPacket* pkt = nullptr;
        while(try_pop(pkt)) {
            av_packet_free(&pkt);
        }
    }
};

#endif //ANDROIDPLAYER_PACKETQUEUE_H