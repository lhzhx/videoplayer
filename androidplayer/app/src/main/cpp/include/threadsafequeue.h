//
// Created by 29406 on 2025/7/10.
//

#ifndef ANDROIDPLAYER_THREADSAFEQUEUE_H
#define ANDROIDPLAYER_THREADSAFEQUEUE_H


#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include "android/log.h"
#define LOG_TAG "threadsafequeue"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

template<typename T>
struct node {
    T data;
    node* next;
    node(const T& value) : data(value), next(nullptr) {}
};

template<typename T>
class threadsafequeue{

private:
    mutable std::mutex mut;
    node<T>* first;
    node<T>* last;
    std::condition_variable data_cond;
    std::condition_variable space_cond;
    size_t current_size;
    //设置最大长度，以防止内存崩溃
    size_t max_size;
    //禁用＝号，不允许拷贝
    threadsafequeue& operator=(const threadsafequeue&) = delete;
public:
    //默认100
    threadsafequeue(size_t maxSize = 100): first(nullptr), last(nullptr), current_size(0), max_size(maxSize){}
    //禁止拷贝
    threadsafequeue(const threadsafequeue& other) = delete;

    void push(const T& new_value){
        node<T>* new_node = new node<T>(new_value);
        std::unique_lock<std::mutex> lk(mut);
        
        // 等待直到有空位
        space_cond.wait(lk, [this]{ return current_size < max_size; });
        
        if (last) {
            last->next = new_node;
            last = new_node;
        } else {
            first = last = new_node;
        }
        current_size++;
        data_cond.notify_one();
    }

    // 带超时的wait_and_pop，避免无限等待
    bool wait_and_pop(T& value, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)){
        std::unique_lock<std::mutex> lk(mut);
        bool flag = data_cond.wait_for(lk, timeout, [this]{return first != nullptr;});
        if(flag && first){
            node<T>* old_first = first;
            value = first->data;
            first = first->next;
            if (!first) last = nullptr;
            current_size--;
            delete old_first;
            space_cond.notify_one();  // 通知有空位了
            return true;
        }
        return false;
    }
    
    // 保留原版本用于兼容性，但添加默认超时
    void wait_and_pop(T& value){
        wait_and_pop(value, std::chrono::milliseconds(100));
    }

    /*线程安全队列中并未使用到的部分
    std::shared_ptr<T> wait_and_pop(){
        std::unique_lock<std::mutex> lk(mut);
        data_cond.wait(lk, [this] { return first != nullptr; });
        node<T>* old_first = first;
        std::shared_ptr<T> res = std::make_shared<T>(first->data);
        first = first->next;
        if (!first) last = nullptr;
        delete old_first;
        return res;
    }
*/
    bool try_pop(T& value){
        std::lock_guard<std::mutex> lk(mut);
        if (!first) return false;
        node<T>* old_first = first;
        value = first->data;
        first = first->next;
        if (!first) last = nullptr;
        current_size--;
        delete old_first;
        space_cond.notify_one();  // 通知有空位了
        return true;
    }

    std::shared_ptr<T> try_pop(){
        std::lock_guard<std::mutex> lk(mut);
        if (!first) return std::shared_ptr<T>();
        node<T>* old_first = first;
        std::shared_ptr<T> res = std::make_shared<T>(first->data);
        first = first->next;
        if (!first) last = nullptr;
        current_size--;
        delete old_first;
        space_cond.notify_one();  // 通知有空位了
        return res;
    }

    bool empty() const{
        std::lock_guard<std::mutex> lk(mut);
        return first == nullptr;
    }
    
    size_t size() const{
        std::lock_guard<std::mutex> lk(mut);
        return current_size;
    }
    
    size_t capacity() const{
        return max_size;
    }
    
    bool full() const{
        std::lock_guard<std::mutex> lk(mut);
        return current_size >= max_size;
    }
};
#endif //ANDROIDPLAYER_THREADSAFEQUEUE_H