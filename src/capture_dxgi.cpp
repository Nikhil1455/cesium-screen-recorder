#include "capture_dxgi.h"
#include "utils.h"
#include <thread>

DXGICapturer::DXGICapturer() = default;

DXGICapturer::~DXGICapturer() {
    release();
}

bool DXGICapturer::is_available() {
    // DXGI Desktop Duplication requires Windows 8+
    int version = get_windows_version();
    return version >= 8;
}

bool DXGICapturer::initialize(int output_index) {
    if (!is_available()) {
        LOG_WARN("DXGI Desktop Duplication not available on this Windows version");
        return false;
    }

    // Create D3D11 device
    if (!create_d3d_device()) {
        LOG_ERROR("Failed to create D3D11 device");
        return false;
    }

    // Find the output (monitor)
    if (!find_output(output_index)) {
        LOG_ERROR("Failed to find output %d", output_index);
        return false;
    }

    // Create staging texture for CPU access
    if (!create_staging_texture()) {
        LOG_ERROR("Failed to create staging texture");
        return false;
    }

    initialized_.store(true, std::memory_order_release);
    LOG_INFO("DXGI Desktop Duplication initialized: %dx%d", width_, height_);
    return true;
}

bool DXGICapturer::create_d3d_device() {
    // Create DXGI factory
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), 
                                     reinterpret_cast<void**>(resources_.factory.GetAddressOf()));
    if (!check_hresult(hr, "CreateDXGIFactory1")) {
        return false;
    }

    // Get first adapter (usually the primary GPU)
    hr = resources_.factory->EnumAdapters1(0, resources_.adapter.GetAddressOf());
    if (!check_hresult(hr, "EnumAdapters1")) {
        return false;
    }

    // Create D3D11 device with BGRA support
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_level;
    hr = D3D11CreateDevice(
        resources_.adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        flags,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        resources_.device.GetAddressOf(),
        &feature_level,
        resources_.context.GetAddressOf()
    );
    
    if (!check_hresult(hr, "D3D11CreateDevice")) {
        // Retry without BGRA support for older systems
        flags &= ~D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(
            resources_.adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            feature_levels,
            ARRAYSIZE(feature_levels),
            D3D11_SDK_VERSION,
            resources_.device.GetAddressOf(),
            &feature_level,
            resources_.context.GetAddressOf()
        );
        if (!check_hresult(hr, "D3D11CreateDevice (retry)")) {
            return false;
        }
    }

    LOG_INFO("D3D11 device created with feature level %d", static_cast<int>(feature_level));
    return true;
}

bool DXGICapturer::find_output(int output_index) {
    // Enumerate outputs to find the specified monitor
    ComPtr<IDXGIOutput> dxgi_output;
    HRESULT hr = resources_.adapter->EnumOutputs(output_index, dxgi_output.GetAddressOf());
    if (!check_hresult(hr, "EnumOutputs")) {
        return false;
    }

    // Get output description
    DXGI_OUTPUT_DESC desc;
    hr = dxgi_output->GetDesc(&desc);
    if (!check_hresult(hr, "GetDesc")) {
        return false;
    }

    // Store dimensions
    width_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
    height_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;

    LOG_INFO("Found output %d: %dx%d at (%d,%d)", 
             output_index, width_, height_,
             desc.DesktopCoordinates.left, 
             desc.DesktopCoordinates.top);

    // Query IDXGIOutput1 for Desktop Duplication
    hr = dxgi_output.As(&resources_.output1);
    if (!check_hresult(hr, "QueryInterface IDXGIOutput1")) {
        return false;
    }

    // Create Desktop Duplication
    hr = resources_.output1->DuplicateOutput(resources_.device.Get(), 
                                              resources_.duplication.GetAddressOf());
    if (!check_hresult(hr, "DuplicateOutput")) {
        // Check for common error codes
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            LOG_ERROR("Too many applications using Desktop Duplication");
        } else if (hr == E_ACCESSDENIED) {
            LOG_ERROR("Access denied - another app may be using exclusive mode");
        }
        return false;
    }

    LOG_INFO("Desktop Duplication acquired successfully");
    return true;
}

bool DXGICapturer::create_staging_texture() {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = resources_.device->CreateTexture2D(&desc, nullptr, 
                                                     resources_.staging_texture.GetAddressOf());
    if (!check_hresult(hr, "CreateTexture2D")) {
        return false;
    }

    LOG_INFO("Staging texture created: %dx%d BGRA", width_, height_);
    return true;
}

bool DXGICapturer::capture_frame(VideoFrame& out_frame, int64_t pts, uint8_t* bgra_buffer) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }

    // Release previous frame
    resources_.duplication->ReleaseFrame();

    // Acquire next frame with timeout (skip vsync sync to avoid stutter)
    IDXGIResource* desktop_resource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frame_info;
    
    // Zero timeout for non-blocking capture - critical for low overhead
    HRESULT hr = resources_.duplication->AcquireNextFrame(0, &frame_info, &desktop_resource);
    
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No new frame available - this is normal
        return false;
    }
    
    if (!check_hresult(hr, "AcquireNextFrame")) {
        // Handle session lost - may need to reinitialize
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOG_WARN("Desktop Duplication session lost - will reinitialize");
            initialized_.store(false, std::memory_order_release);
        }
        return false;
    }

    // Get the texture from the desktop resource
    ComPtr<ID3D11Texture2D> desktop_texture;
    hr = desktop_resource->QueryInterface(__uuidof(ID3D11Texture2D), 
                                           reinterpret_cast<void**>(desktop_texture.GetAddressOf()));
    desktop_resource->Release();
    
    if (!check_hresult(hr, "QueryInterface ID3D11Texture2D")) {
        return false;
    }

    // Copy GPU texture to staging texture for CPU access
    resources_.context->CopyResource(resources_.staging_texture.Get(), desktop_texture.Get());

    // Map staging texture to CPU memory
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = resources_.context->Map(resources_.staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (!check_hresult(hr, "Map")) {
        return false;
    }

    // Copy BGRA data (skip alpha channel as requested)
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    const size_t row_bytes = width_ * 4;  // BGRA = 4 bytes per pixel
    
    for (int y = 0; y < height_; ++y) {
        memcpy(bgra_buffer + y * row_bytes, src + y * mapped.RowPitch, row_bytes);
    }

    resources_.context->Unmap(resources_.staging_texture.Get(), 0);

    // Set output frame metadata
    out_frame.width = width_;
    out_frame.height = height_;
    out_frame.pts = pts;
    out_frame.is_keyframe = frame_info.LastPresentTime.QuadPart != 0;  // Simplified keyframe detection
    out_frame.valid = true;

    return true;
}

void DXGICapturer::release() {
    if (resources_.duplication) {
        resources_.duplication->ReleaseFrame();
    }
    resources_.release_all();
    initialized_.store(false, std::memory_order_release);
    LOG_INFO("DXGI resources released");
}
