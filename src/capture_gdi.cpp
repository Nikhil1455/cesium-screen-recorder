#include "capture_gdi.h"
#include "utils.h"
#include <cstring>

GDICapturer::GDICapturer() = default;

GDICapturer::~GDICapturer() {
    release();
}

bool GDICapturer::initialize(HWND target_window) {
    target_window_ = target_window;

    // Get screen dimensions
    if (target_window_) {
        RECT rect;
        if (!GetWindowRect(target_window_, &rect)) {
            LOG_ERROR("Failed to get window rect");
            return false;
        }
        width_ = rect.right - rect.left;
        height_ = rect.bottom - rect.top;
    } else {
        // Full screen capture
        width_ = GetSystemMetrics(SM_CXSCREEN);
        height_ = GetSystemMetrics(SM_CYSCREEN);
    }

    LOG_INFO("GDI capture initialized: %dx%d", width_, height_);

    // Get screen DC
    resources_.screen_dc = GetDC(nullptr);
    if (!resources_.screen_dc) {
        LOG_ERROR("Failed to get screen DC");
        return false;
    }

    // Create memory DC for double-buffering
    resources_.memory_dc = CreateCompatibleDC(resources_.screen_dc);
    if (!resources_.memory_dc) {
        LOG_ERROR("Failed to create memory DC");
        ReleaseDC(nullptr, resources_.screen_dc);
        resources_.screen_dc = nullptr;
        return false;
    }

    // Create compatible bitmap
    resources_.bitmap = CreateCompatibleBitmap(resources_.screen_dc, width_, height_);
    if (!resources_.bitmap) {
        LOG_ERROR("Failed to create bitmap");
        DeleteDC(resources_.memory_dc);
        ReleaseDC(nullptr, resources_.screen_dc);
        resources_.memory_dc = nullptr;
        resources_.screen_dc = nullptr;
        return false;
    }

    // Select bitmap into memory DC
    resources_.old_bitmap = static_cast<HBITMAP>(SelectObject(resources_.memory_dc, resources_.bitmap));
    if (!resources_.old_bitmap) {
        LOG_ERROR("Failed to select bitmap into DC");
        DeleteObject(resources_.bitmap);
        DeleteDC(resources_.memory_dc);
        ReleaseDC(nullptr, resources_.screen_dc);
        resources_.bitmap = nullptr;
        resources_.memory_dc = nullptr;
        resources_.screen_dc = nullptr;
        return false;
    }

    // Allocate back buffer for double-buffering
    back_buffer_size_ = static_cast<size_t>(width_) * height_ * 4;  // BGRA
    back_buffer_ = new uint8_t[back_buffer_size_];

    initialized_.store(true, std::memory_order_release);
    LOG_INFO("GDI capture ready with double-buffering");
    return true;
}

bool GDICapturer::capture_frame(uint8_t* dest_buffer, int64_t pts) {
    (void)pts;  // GDI doesn't use PTS
    
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    // Perform BitBlt from screen to memory DC
    BOOL result;
    if (target_window_) {
        // Capture specific window
        HDC window_dc = GetDC(target_window_);
        if (!window_dc) {
            return false;
        }
        result = BitBlt(resources_.memory_dc, 0, 0, width_, height_, 
                       window_dc, 0, 0, SRCCOPY);
        ReleaseDC(target_window_, window_dc);
    } else {
        // Full screen capture
        result = BitBlt(resources_.memory_dc, 0, 0, width_, height_,
                       resources_.screen_dc, 0, 0, SRCCOPY);
    }

    if (!result) {
        LOG_DEBUG("BitBlt failed");
        return false;
    }

    // Get bitmap bits using GetDIBits (more efficient than GetPixel)
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = width_;
    bih.biHeight = -height_;  // Negative for top-down bitmap
    bih.biPlanes = 1;
    bih.biBitCount = 32;  // BGRA
    bih.biCompression = BI_RGB;

    int lines = GetDIBits(resources_.memory_dc, resources_.bitmap, 0, height_,
                         back_buffer_, reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);
    
    if (lines != height_) {
        LOG_DEBUG("GetDIBits failed: got %d lines, expected %d", lines, height_);
        return false;
    }

    // Copy from back buffer to destination (double-buffering)
    memcpy(dest_buffer, back_buffer_, back_buffer_size_);

    return true;
}

void GDICapturer::release() {
    if (back_buffer_) {
        delete[] back_buffer_;
        back_buffer_ = nullptr;
        back_buffer_size_ = 0;
    }

    resources_.release_all();
    
    if (resources_.screen_dc) {
        ReleaseDC(nullptr, resources_.screen_dc);
        resources_.screen_dc = nullptr;
    }
    
    initialized_.store(false, std::memory_order_release);
    LOG_INFO("GDI resources released");
}
