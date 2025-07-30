#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <memory>
#include <deque>
#include <algorithm>
#include "network_client.h"
#include "AudioFrameQueue.h"
#include "AudioOutput.h"
#include "FrameQueue.h"
#include "VideoRenderer.h"
#include <alsa/asoundlib.h>
#include "MediaDecoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>\n"
                  << "   or: " << argv[0] << " --network <server_ip> <port>" << std::endl;
        return 1;
    }
    bool is_network_mode = (argc >= 2 && std::string(argv[1]) == "--network");
    std::string filename, server_ip;
    int server_port = 0;
    if (is_network_mode) {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " --network <server_ip> <port>" << std::endl;
            return 1;
        }
        server_ip = argv[2];
        server_port = std::stoi(argv[3]);
    } else {
        filename = argv[1];
    }
    MediaDecoder decoder;
    bool ok = is_network_mode ? decoder.openNetwork(server_ip, server_port) : decoder.open(filename);
    if (!ok) {
        std::cerr << "Failed to open media source." << std::endl;
        return 1;
    }
    VideoRenderer renderer(decoder.width(), decoder.height());
    AudioOutput audio_output;
    if (!audio_output.initialize(decoder.sampleRate(), decoder.channels())) {
        std::cerr << "Failed to initialize audio output" << std::endl;
        return 1;
    }
    std::atomic<bool> quit(false);
    std::atomic<bool> paused(false);
    static bool last_space = false, last_q = false, last_right = false, last_left = false;
    int seek_forward_sec = 5, seek_backward_sec = 5;
    double cur_pos_sec = 0.0;
    while (!renderer.shouldClose()) {
        if (quit) break;
        bool cur_q = glfwGetKey(renderer.getWindow(), GLFW_KEY_Q) == GLFW_PRESS;
        if (cur_q && !last_q) {
            quit = true;
            glfwSetWindowShouldClose(renderer.getWindow(), GLFW_TRUE);
        }
        last_q = cur_q;
        bool cur_space = glfwGetKey(renderer.getWindow(), GLFW_KEY_SPACE) == GLFW_PRESS;
        if (cur_space && !last_space) {
            paused = !paused;
            std::cout << (paused ? "Paused" : "Playing") << std::endl;
        }
        last_space = cur_space;
        if (!decoder.isNetworkMode()) {
            bool cur_right = glfwGetKey(renderer.getWindow(), GLFW_KEY_RIGHT) == GLFW_PRESS;
            if (cur_right && !last_right) {
                cur_pos_sec += seek_forward_sec;
                decoder.seek(cur_pos_sec);
                decoder.flush();
                std::cout << "Seek forward " << seek_forward_sec << "s, now at " << cur_pos_sec << "s" << std::endl;
            }
            last_right = cur_right;
            bool cur_left = glfwGetKey(renderer.getWindow(), GLFW_KEY_LEFT) == GLFW_PRESS;
            if (cur_left && !last_left) {
                cur_pos_sec -= seek_backward_sec;
                if (cur_pos_sec < 0) cur_pos_sec = 0;
                decoder.seek(cur_pos_sec);
                decoder.flush();
                std::cout << "Seek backward " << seek_backward_sec << "s, now at " << cur_pos_sec << "s" << std::endl;
            }
            last_left = cur_left;
        }
        if (paused) {
            renderer.pollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (decoder.readFrame()) {
            AVFrame* frame = decoder.getVideoFrame();
            if (frame) {
                renderer.updateFrame(frame);
                renderer.render();
                av_frame_free(&frame);
            }
            AudioFrame aframe = decoder.getAudioFrame();
            if (!aframe.samples.empty()) {
                audio_output.play(aframe.samples);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        renderer.pollEvents();
    }
    decoder.close();
    audio_output.close();
    glfwDestroyWindow(renderer.getWindow());
    glfwTerminate();
    return 0;
} 