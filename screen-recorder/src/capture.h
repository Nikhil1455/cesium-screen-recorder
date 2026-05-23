// capture.h - Screen capture interface (DXGI + GDI fallback)
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <memory>

// Forward declare FFmpeg types
struct AVFrame;
struct AVBufferRef;

// Frame data structure for queue transfer
struct CapturedFrame {
    uint8_t* data[4];      // Plane pointers
    int linesize[4];       // Linesizes
    int width;
    int height;
    int64_t pts;           // Presentation timestamp
    bool keyframe;
    
    // For cleanup
    AVBufferRef* bufferRef;
    
    CapturedFrame() : bufferRef(nullptr) {
        ZeroMemory(data, sizeof(data));
        ZeroMemory(linesize, sizeof(linesize));
        width = height = 0;
        pts = 0;
        keyframe = false;
    }
};

class ScreenCapture {
public:
    virtual ~ScreenCapture() = default;
    
    // Initialize capture
    virtual bool Initialize(int width, int height, int fps) = 0;
    
    // Capture a single frame (called from capture thread)
    // Returns true if frame captured, false on error
    virtual bool CaptureFrame(CapturedFrame& frame) = 0;
    
    // Release frame resources after encoding
    virtual void ReleaseFrame(CapturedFrame& frame) = 0;
    
    // Get capture dimensions
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual int FPS() const = 0;
    
    // Static factory - returns DXGI or GDI based on OS support
    static std::unique_ptr<ScreenCapture> Create();
};

// DXGI Desktop Duplication implementation (Win8+)
class DXGICapture : public ScreenCapture {
public:
    DXGICapture();
    ~DXGICapture() override;

    bool Initialize(int width, int height, int fps) override;
    bool CaptureFrame(CapturedFrame& frame) override;
    void ReleaseFrame(CapturedFrame& frame) override;
    
    int Width() const override { return width_; }
    int Height() const override { return height_; }
    int FPS() const override { return fps_; }

private:
    bool InitializeD3D();
    bool InitializeDuplication();
    void Cleanup();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    
    int width_;
    int height_;
    int fps_;
    bool initialized_;
    
    // Preallocated staging texture for CPU access
    ComPtr<ID3D11Texture2D> stagingTexture_;
};

// GDI BitBlt implementation (Win7 fallback)
class GDICapture : public ScreenCapture {
public:
    GDICapture();
    ~GDICapture() override;

    bool Initialize(int width, int height, int fps) override;
    bool CaptureFrame(CapturedFrame& frame) override;
    void ReleaseFrame(CapturedFrame& frame) override;
    
    int Width() const override { return width_; }
    int Height() const override { return height_; }
    int FPS() const override { return fps_; }

private:
    void Cleanup();

    HDC screenDC_;
    HDC memoryDC_;
    HBITMAP bitmap_;
    HBITMAP oldBitmap_;
    
    // Double buffering
    uint8_t* frameBuffer_;
    size_t bufferSize_;
    
    int width_;
    int height_;
    int fps_;
    bool initialized_;
};

// Helper template for COM pointers (MinGW compatible)
template<typename T>
class ComPtr {
public:
    ComPtr() : ptr_(nullptr) {}
    
    ~ComPtr() { Reset(); }
    
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    T* Get() const { return ptr_; }
    
    T** GetAddressOf() { return &ptr_; }
    
    T* Detach() {
        T* temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }
    
    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }
    
    T* operator->() const { return ptr_; }
    
    explicit operator bool() const { return ptr_ != nullptr; }
    
private:
    T* ptr_;
};
