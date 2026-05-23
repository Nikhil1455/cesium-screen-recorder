// queue.h - Lock-free SPSC queue for frame transfer
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <new>

template<typename T, size_t Capacity>
class LockFreeSPSCQueue {
public:
    LockFreeSPSCQueue() : head_(0), tail_(0) {}

    // Returns true if push succeeded, false if queue is full (drops oldest)
    bool push(T item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (tail + 1) % Capacity;
        
        // Check if queue is full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            // Queue full - drop oldest (overwrite head)
            head_ = (head_.load(std::memory_order_relaxed) + 1) % Capacity;
        }
        
        buffer_[tail] = std::move(item);
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Returns true if pop succeeded, false if queue is empty
    bool pop(T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        
        if (head == tail_.load(std::memory_order_acquire)) {
            return false; // Empty
        }
        
        item = std::move(buffer_[head]);
        head_.store((head + 1) % Capacity, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (tail >= head) ? (tail - head) : (Capacity - head + tail);
    }

private:
    std::array<T, Capacity> buffer_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};
