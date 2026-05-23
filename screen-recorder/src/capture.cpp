// capture.cpp - Screen capture implementation
#include "capture.h"
#include "utils.h"
#include <dxgi.h>
#include <algorithm>

// Check if DXGI Desktop Duplication is available (Win8+)
static bool IsDXGIDuplicationAvailable() {
    // Windows 8 and later support Desktop Duplication
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 2; // Windows 8
    
    DWORDLONG conditionMask = 0;
    VER_SET_CONDITION(conditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(conditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
    
    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION, conditionMask) != FALSE;
}

std::unique_ptr<ScreenCapture> ScreenCapture::Create() {
    if (IsDXGIDuplicationAvailable()) {
        LOG_DEBUG("Using DXGI Desktop Duplication");
        return std::make_unique<DXGICapture>();
    } else {
        LOG_DEBUG("Using GDI BitBlt fallback");
        return std::make_unique<GDICapture>();
    }
}

// ============================================================================
// DXGICapture Implementation
// ============================================================================

DXGICapture::DXGICapture() 
    : width_(1920), height_(1080), fps_(30), initialized_(false) {}

DXGICapture::~DXGICapture() {
    Cleanup();
}

bool DXGICapture::InitializeD3D() {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    
    D3D_FEATURE_LEVEL featureLevel;
    
    // MinGW-specific: Use D3D11CreateDevice with proper flags
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // Adapter (default)
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // Software module
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,  // Enable video support
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        device_.GetAddressOf(),
        &featureLevel,
        context_.GetAddressOf()
    );
    
    if (FAILED(hr)) {
        LogError("D3D11CreateDevice failed");
        return false;
    }
    
    LOG_DEBUG("D3D11 device created, feature level: %x", featureLevel);
    return true;
}

bool DXGICapture::InitializeDuplication() {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDevice.GetAddressOf()));
    if (FAILED(hr)) {
        LogError("Failed to get IDXGIDevice");
        return false;
    }
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        LogError("Failed to get adapter");
        return false;
    }
    
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, output.GetAddressOf());
    if (FAILED(hr)) {
        LogError("No display outputs found");
        return false;
    }
    
    ComPtr<IDXGIOutput1> output1;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(output1.GetAddressOf()));
    if (FAILED(hr)) {
        LogError("Failed to get IDXGIOutput1");
        return false;
    }
    
    // Get output description for dimensions
    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);
    width_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
    
    LOG_DEBUG("Output resolution: %dx%d", width_, height_);
    
    // Create duplication
    hr = output1->DuplicateOutput(device_.Get(), duplication_.GetAddressOf());
    if (FAILED(hr)) {
        LogError("DuplicateOutput failed");
        return false;
    }
    
    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = width_;
    stagingDesc.Height = height_;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    hr = device_->CreateTexture2D(&stagingDesc, nullptr, stagingTexture_.GetAddressOf());
    if (FAILED(hr)) {
        LogError("Failed to create staging texture");
        return false;
    }
    
    return true;
}

bool DXGICapture::Initialize(int width, int height, int fps) {
    (void)width; (void)height; // Use actual screen resolution
    
    if (initialized_) {
        return true;
    }
    
    if (!InitializeD3D()) {
        return false;
    }
    
    if (!InitializeDuplication()) {
        return false;
    }
    
    fps_ = fps;
    initialized_ = true;
    LOG_DEBUG("DXGI capture initialized: %dx%d @ %dfps", width_, height_, fps_);
    return true;
}

bool DXGICapture::CaptureFrame(CapturedFrame& frame) {
    if (!initialized_ || !duplication_) {
        return false;
    }
    
    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    
    // Acquire next frame (timeout 100ms)
    HRESULT hr = duplication_->AcquireNextFrame(100, &frameInfo, &desktopResource);
    
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false; // No new frame available
    }
    
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LogError("Desktop duplication lost, needs reinitialization");
            Cleanup();
        }
        return false;
    }
    
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), 
                                          reinterpret_cast<void**>(desktopTexture.GetAddressOf()));
    desktopResource->Release();
    
    if (FAILED(hr)) {
        return false;
    }
    
    // Copy to staging texture
    context_->CopyResource(stagingTexture_.Get(), desktopTexture.Get());
    
    // Map staging texture for CPU access
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return false;
    }
    
    // Set up frame data (BGRA format from DXGI)
    frame.width = width_;
    frame.height = height_;
    frame.data[0] = static_cast<uint8_t*>(mapped.pData);
    frame.linesize[0] = mapped.RowPitch;
    frame.pts = frameInfo.LastPresentTime.QuadPart;
    frame.keyframe = false; // Will be set by encoder
    
    // Note: We don't unmap here - ReleaseFrame will do it after encoding
    // This is a simplification; production code should copy to owned buffer
    
    duplication_->ReleaseFrame();
    return true;
}

void DXGICapture::ReleaseFrame(CapturedFrame& frame) {
    if (context_ && stagingTexture_) {
        context_->Unmap(stagingTexture_.Get(), 0);
    }
    
    frame.data[0] = nullptr;
    frame.linesize[0] = 0;
}

void DXGICapture::Cleanup() {
    if (duplication_) {
        duplication_->ReleaseFrame();
        duplication_.Reset();
    }
    stagingTexture_.Reset();
    context_.Reset();
    device_.Reset();
    initialized_ = false;
}

// ============================================================================
// GDICapture Implementation  
// ============================================================================

GDICapture::GDICapture()
    : screenDC_(nullptr), memoryDC_(nullptr), bitmap_(nullptr), oldBitmap_(nullptr),
      frameBuffer_(nullptr), bufferSize_(0), width_(1920), height_(1080), 
      fps_(30), initialized_(false) {}

GDICapture::~GDICapture() {
    Cleanup();
}

bool GDICapture::Initialize(int width, int height, int fps) {
    if (initialized_) {
        return true;
    }
    
    width_ = width;
    height_ = height;
    fps_ = fps;
    
    // Get screen DC
    screenDC_ = GetDC(nullptr);
    if (!screenDC_) {
        LogError("Failed to get screen DC");
        return false;
    }
    
    // Create compatible DC and bitmap
    memoryDC_ = CreateCompatibleDC(screenDC_);
    if (!memoryDC_) {
        ReleaseDC(nullptr, screenDC_);
        LogError("Failed to create memory DC");
        return false;
    }
    
    // Create DIB section for direct memory access
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = width_;
    bih.biHeight = -height_; // Top-down DIB
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    
    BITMAPINFO bi = {};
    bi.bmiHeader = bih;
    
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(screenDC_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap_) {
        LogError("Failed to create DIB section");
        Cleanup();
        return false;
    }
    
    oldBitmap_ = static_cast<HBITMAP>(SelectObject(memoryDC_, bitmap_));
    frameBuffer_ = static_cast<uint8_t*>(bits);
    bufferSize_ = static_cast<size_t>(width_ * height_ * 4);
    
    initialized_ = true;
    LOG_DEBUG("GDI capture initialized: %dx%d @ %dfps", width_, height_, fps_);
    return true;
}

bool GDICapture::CaptureFrame(CapturedFrame& frame) {
    if (!initialized_) {
        return false;
    }
    
    // BitBlt from screen to memory DC
    BOOL result = BitBlt(memoryDC_, 0, 0, width_, height_, screenDC_, 0, 0, SRCCOPY);
    if (!result) {
        return false;
    }
    
    // Set up frame data
    frame.width = width_;
    frame.height = height_;
    frame.data[0] = frameBuffer_;
    frame.linesize[0] = width_ * 4; // BGRA, 4 bytes per pixel
    frame.pts = 0; // Will be set by encoder
    frame.keyframe = false;
    
    return true;
}

void GDICapture::ReleaseFrame(CapturedFrame& frame) {
    // GDI frames use shared buffer, no cleanup needed
    (void)frame;
}

void GDICapture::Cleanup() {
    if (oldBitmap_ && memoryDC_) {
        SelectObject(memoryDC_, oldBitmap_);
        oldBitmap_ = nullptr;
    }
    
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    
    if (memoryDC_) {
        DeleteDC(memoryDC_);
        memoryDC_ = nullptr;
    }
    
    if (screenDC_) {
        ReleaseDC(nullptr, screenDC_);
        screenDC_ = nullptr;
    }
    
    frameBuffer_ = nullptr;
    initialized_ = false;
}
