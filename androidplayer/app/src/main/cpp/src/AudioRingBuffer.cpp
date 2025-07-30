#include "AudioRingBuffer.h"

AudioRingBuffer::AudioRingBuffer(size_t size) 
    : capacity(size), writePos(0), readPos(0), stopped(false) {
    buffer = new uint8_t[capacity];  // 分配缓冲区内存
    memset(buffer, 0, capacity);     // 清零初始化
}

AudioRingBuffer::~AudioRingBuffer() {
    delete[] buffer;
}

size_t AudioRingBuffer::write(const uint8_t* data, size_t size, std::chrono::milliseconds timeout) {
    // 参数有效性检查
    if (!data || size == 0 || stopped.load()) {
        return 0;
    }
    
    std::unique_lock<std::mutex> lock(mut);
    
    // 等待有足够空间写入，使用条件变量避免忙等待
    bool hasSpace = writeCond.wait_for(lock, timeout, [this, size] {
        return getAvailableSpace() >= size || stopped.load();
    });
    
    // 检查是否因停止或超时而退出等待
    if (stopped.load() || !hasSpace) {
        return 0;
    }
    
    // 计算实际可写入的数据大小
    size_t actualSize = std::min(size, getAvailableSpace());
    size_t firstPart = std::min(actualSize, capacity - writePos);  // 第一部分：到缓冲区末尾
    size_t secondPart = actualSize - firstPart;                   // 第二部分：从缓冲区开头
    
    // 执行环形写入操作
    memcpy(buffer + writePos, data, firstPart);  // 写入第一部分
    if (secondPart > 0) {  // 如果需要回绕写入
        memcpy(buffer, data + firstPart, secondPart);
    }
    writePos = (writePos + actualSize) % capacity;  // 更新写入位置
    
    // 通知读取线程有新数据可读
    readCond.notify_one();
    return actualSize;
}

size_t AudioRingBuffer::read(uint8_t* data, size_t size, std::chrono::milliseconds timeout) {
    // 参数有效性检查
    if (!data || size == 0) {
        return 0;
    }
    
    std::unique_lock<std::mutex> lock(mut);
    
    // 等待有足够数据读取，使用条件变量避免忙等待
    bool hasData = readCond.wait_for(lock, timeout, [this, size] {
        return getAvailableData() >= size || stopped.load();
    });
    
    // 如果已停止且无数据，直接返回
    if (stopped.load() && getAvailableData() == 0) {
        return 0;
    }
    
    // 计算实际可读取的数据大小
    size_t actualSize = std::min(size, getAvailableData());
    if (actualSize == 0) {
        return 0;
    }
    
    size_t firstPart = std::min(actualSize, capacity - readPos);  // 第一部分：到缓冲区末尾
    size_t secondPart = actualSize - firstPart;                  // 第二部分：从缓冲区开头
    
    // 执行环形读取操作
    memcpy(data, buffer + readPos, firstPart);  // 读取第一部分
    if (secondPart > 0) {  // 如果需要回绕读取
        memcpy(data + firstPart, buffer, secondPart);
    }
    readPos = (readPos + actualSize) % capacity;  // 更新读取位置
    
    // 通知写入线程有新空间可写
    writeCond.notify_one();
    
    return actualSize;
}

size_t AudioRingBuffer::tryRead(uint8_t* data, size_t size) {
    // 参数有效性检查
    if (!data || size == 0) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mut);  // 使用简单锁，不需要条件变量
    
    // 计算实际可读取的数据大小（不等待）
    size_t actualSize = std::min(size, getAvailableData());
    if (actualSize == 0) {
        return 0;  // 无数据可读，立即返回
    }
    
    size_t firstPart = std::min(actualSize, capacity - readPos);  // 第一部分：到缓冲区末尾
    size_t secondPart = actualSize - firstPart;                  // 第二部分：从缓冲区开头
    
    // 执行环形读取操作
    memcpy(data, buffer + readPos, firstPart);  // 读取第一部分
    if (secondPart > 0) {  // 如果需要回绕读取
        memcpy(data + firstPart, buffer, secondPart);
    }
    readPos = (readPos + actualSize) % capacity;  // 更新读取位置
    
    // 通知写入线程有新空间可写
    writeCond.notify_one();
    
    return actualSize;
}

size_t AudioRingBuffer::getAvailableData() const {
    if (writePos >= readPos) {
        return writePos - readPos;  // 写入位置在读取位置之后
    } else {
        return capacity - readPos + writePos;  // 写入位置回绕到开头
    }
}

//用空出一个字节，保证能区分满和空
size_t AudioRingBuffer::getAvailableSpace() const {
    // 保留一个字节以区分满和空的状态
    return capacity - getAvailableData() - 1;
}


void AudioRingBuffer::flush() {
    std::lock_guard<std::mutex> lock(mut);
    readPos = 0;                          // 重置读取位置
    writePos = 0;                         // 重置写入位置
    memset(buffer, 0, capacity);          // 清零缓冲区内容
    writeCond.notify_all();               // 唤醒所有等待写入的线程
    readCond.notify_all();                // 唤醒所有等待读取的线程
}


//当读取位置等于写入位置时，缓冲区为空。
bool AudioRingBuffer::isEmpty() const {
    std::lock_guard<std::mutex> lock(mut);
    return readPos == writePos;
}

//当可用空间为0时，缓冲区已满。

bool AudioRingBuffer::isFull() const {
    std::lock_guard<std::mutex> lock(mut);
    return getAvailableSpace() == 0;
}

//停止缓冲区操作
void AudioRingBuffer::stop() {
    stopped.store(true);      // 设置原子停止标志
    readCond.notify_all();    // 唤醒所有等待读取的线程
    writeCond.notify_all();   // 唤醒所有等待写入的线程
}

//检查缓冲区是否已停止
bool AudioRingBuffer::isStopped() const {
    return stopped.load();
}