#pragma once

#include <Windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <atomic>

#include "ring_buffer.h"

using Microsoft::WRL::ComPtr;

// Forward declaration
class FramePool;

// DXGI Desktop Duplication capture for Win8/10/11
// Optimized for Intel HD 5500 iGPU with GPU-resident frames
class DXGICapturer {
public:
    DXGICapturer();
    ~DXGICapturer();

    // Initialize DXGI capture
    bool initialize(int output_index = 0);
    
    // Capture a single frame (called from capture thread)
    // Returns true on success, false on failure or timeout
    bool capture_frame(VideoFrame& out_frame, int64_t pts, uint8_t* bgra_buffer);
    
    // Get screen dimensions
    int get_width() const { return width_; }
    int get_height() const { return height_; }
    
    // Check if DXGI is available (Win8+)
    static bool is_available();
    
    // Cleanup resources
    void release();

private:
    // RAII wrappers for DXGI resources
    class DXGIResources {
    public:
        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput1> output1;
        ComPtr<IDXGIOutputDuplication> duplication;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11Texture2D> staging_texture;
        
        void release_all() {
            duplication.Reset();
            staging_texture.Reset();
            context.Reset();
            device.Reset();
            output1.Reset();
            output.Reset();
            adapter.Reset();
            factory.Reset();
        }
    };

    bool create_d3d_device();
    bool find_output(int output_index);
    bool create_staging_texture();
    void map_texture_to_buffer(ID3D11Texture2D* texture, uint8_t* dest_buffer);

    DXGIResources resources_;
    int width_ = 0;
    int height_ = 0;
    std::atomic<bool> initialized_{false};
    UINT dpi_awareness_context_ = 0;
};
