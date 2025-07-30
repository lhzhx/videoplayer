//
// Created by 29406 on 2025/7/10.
//

#ifndef ANDROIDPLAYER_FRAMEQUEUE_H
#define ANDROIDPLAYER_FRAMEQUEUE_H

#include "threadsafequeue.h"
extern "C"{
#include <libavutil/frame.h>
}

class Framequeue : public threadsafequeue<AVFrame*> {
public:
    void put(AVFrame* frame) {
        AVFrame* f = av_frame_alloc();
        av_frame_ref(f, frame);
        push(f);
    }

    AVFrame* get() {
        AVFrame* frame = nullptr;
        if (wait_and_pop(frame, std::chrono::milliseconds(100))) {
            return frame;
        }
        return nullptr;
    }

    void flush() {
        AVFrame* frame = nullptr;
        while(try_pop(frame)) {
            av_frame_free(&frame);
        }
    }
};

#endif //ANDROIDPLAYER_FRAMEQUEUE_H