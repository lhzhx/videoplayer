//
// Created for AudioPlayer
//

#ifndef ANDROIDPLAYER_AUDIORINGBUFFER_H
#define ANDROIDPLAYER_AUDIORINGBUFFER_H

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "AudioRingBuffer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class AudioRingBuffer {
private:
    uint8_t* buffer;
    size_t capacity;
    size_t writePos;
    size_t readPos;
    mutable std::mutex mut;
    std::condition_variable readCond;
    std::condition_variable writeCond;
    std::atomic<bool> stopped;
    
public:
    AudioRingBuffer(size_t size = 1024 * 1024); // 默认1MB缓冲区
    ~AudioRingBuffer();
    
    // 写入PCM数据，返回实际写入的字节数
    size_t write(const uint8_t* data, size_t size, std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
    
    // 读取PCM数据，返回实际读取的字节数
    size_t read(uint8_t* data, size_t size, std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
    
    // 尝试读取数据，不阻塞
    size_t tryRead(uint8_t* data, size_t size);
    
    // 获取可用数据大小
    size_t getAvailableData() const;
    
    // 获取可用空间大小
    size_t getAvailableSpace() const;
    
    // 清空缓冲区
    void flush();
    
    // 检查是否为空
    bool isEmpty() const;
    
    // 检查是否已满
    bool isFull() const;
    
    // 停止缓冲区操作
    void stop();
    
    // 检查是否已停止
    bool isStopped() const;
};

#endif //ANDROIDPLAYER_AUDIORINGBUFFER_H