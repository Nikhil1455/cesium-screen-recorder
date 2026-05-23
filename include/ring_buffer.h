#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <new>

// Cache line size for padding to avoid false sharing
constexpr size_t CACHE_LINE_SIZE = 64;

// Fixed-size frame pool - zero dynamic allocation during recording
template<typename T, size_t Capacity>
class LockFreeSPSCRingBuffer {
public:
    LockFreeSPSCRingBuffer() : head_(0), tail_(0) {
        // Preallocate all frames at construction - no malloc in hot path
        for (size_t i = 0; i < Capacity; ++i) {
            pool_[i] = T{};
        }
    }

    // Producer pushes frame to buffer. Returns nullptr if full (drop oldest policy).
    T* try_push() {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) % Capacity;
        
        // Check if buffer is full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            // Buffer full - drop oldest frame (do nothing, caller handles)
            return nullptr;
        }
        
        T* frame = &pool_[current_tail];
        tail_.store(next_tail, std::memory_order_release);
        return frame;
    }

    // Consumer pops frame from buffer. Returns nullptr if empty.
    T* try_pop() {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        
        T* frame = &pool_[current_head];
        head_.store((current_head + 1) % Capacity, std::memory_order_release);
        return frame;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if (tail >= head) {
            return tail - head;
        }
        return Capacity - head + tail;
    }

private:
    // Cache-line aligned atomic indices to prevent false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
    alignas(CACHE_LINE_SIZE) std::array<T, Capacity> pool_;
};

// Frame structure for video data
struct VideoFrame {
    uint8_t* data[4] = {nullptr, nullptr, nullptr, nullptr};
    int linesize[4] = {0, 0, 0, 0};
    int width = 0;
    int height = 0;
    int64_t pts = 0;
    bool is_keyframe = false;
    bool valid = false;
    
    void reset() {
        for (int i = 0; i < 4; ++i) {
            data[i] = nullptr;
            linesize[i] = 0;
        }
        width = 0;
        height = 0;
        pts = 0;
        is_keyframe = false;
        valid = false;
    }
};

// Preallocated frame buffer with fixed memory pools
class FramePool {
public:
    static constexpr size_t MAX_FRAMES = 4;
    static constexpr size_t MAX_WIDTH = 1920;
    static constexpr size_t MAX_HEIGHT = 1080;
    static constexpr size_t BGRA_SIZE = MAX_WIDTH * MAX_HEIGHT * 4;
    static constexpr size_t Y_SIZE = MAX_WIDTH * MAX_HEIGHT;
    static constexpr size_t UV_SIZE = MAX_WIDTH * MAX_HEIGHT / 2;

    FramePool() {
        // Preallocate all memory upfront - zero allocations during recording
        for (size_t i = 0; i < MAX_FRAMES; ++i) {
            bgra_buffers_[i] = new uint8_t[BGRA_SIZE];
            y_buffers_[i] = new uint8_t[Y_SIZE];
            u_buffers_[i] = new uint8_t[UV_SIZE];
            v_buffers_[i] = new uint8_t[UV_SIZE];
            
            frames_[i].data[0] = y_buffers_[i];
            frames_[i].data[1] = u_buffers_[i];
            frames_[i].data[2] = v_buffers_[i];
            frames_[i].linesize[0] = MAX_WIDTH;
            frames_[i].linesize[1] = MAX_WIDTH / 2;
            frames_[i].linesize[2] = MAX_WIDTH / 2;
            frames_[i].width = MAX_WIDTH;
            frames_[i].height = MAX_HEIGHT;
        }
    }

    ~FramePool() {
        for (size_t i = 0; i < MAX_FRAMES; ++i) {
            delete[] bgra_buffers_[i];
            delete[] y_buffers_[i];
            delete[] u_buffers_[i];
            delete[] v_buffers_[i];
        }
    }

    VideoFrame& get_frame(size_t index) {
        return frames_[index];
    }

    uint8_t* get_bgra_buffer(size_t index) {
        return bgra_buffers_[index];
    }

private:
    std::array<VideoFrame, MAX_FRAMES> frames_;
    std::array<uint8_t*, MAX_FRAMES> bgra_buffers_;
    std::array<uint8_t*, MAX_FRAMES> y_buffers_;
    std::array<uint8_t*, MAX_FRAMES> u_buffers_;
    std::array<uint8_t*, MAX_FRAMES> v_buffers_;
};
