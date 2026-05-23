#pragma once

#include <Windows.h>
#include <string>
#include <atomic>

#include "ring_buffer.h"

// GDI BitBlt capture for Windows 7 fallback
// Uses double-buffering to reduce flicker and CPU usage
class GDICapturer {
public:
    GDICapturer();
    ~GDICapturer();

    // Initialize GDI capture
    bool initialize(HWND target_window = nullptr);
    
    // Capture a single frame (called from capture thread)
    // Returns true on success, false on failure
    bool capture_frame(uint8_t* dest_buffer, int64_t pts);
    
    // Get screen dimensions
    int get_width() const { return width_; }
    int get_height() const { return height_; }
    
    // Cleanup resources
    void release();

private:
    class GDIResources {
    public:
        HDC screen_dc = nullptr;
        HDC memory_dc = nullptr;
        HBITMAP bitmap = nullptr;
        HBITMAP old_bitmap = nullptr;
        
        void release_all() {
            if (old_bitmap && memory_dc) {
                SelectObject(memory_dc, old_bitmap);
                old_bitmap = nullptr;
            }
            if (bitmap) {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }
            if (memory_dc) {
                DeleteDC(memory_dc);
                memory_dc = nullptr;
            }
            // Don't delete screen_dc - it's released by system
            screen_dc = nullptr;
        }
    };

    GDIResources resources_;
    int width_ = 0;
    int height_ = 0;
    HWND target_window_ = nullptr;
    std::atomic<bool> initialized_{false};
    
    // Double-buffering support
    uint8_t* back_buffer_ = nullptr;
    size_t back_buffer_size_ = 0;
};
