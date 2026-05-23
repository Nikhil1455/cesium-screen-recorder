// main.cpp - Main entry point and recording orchestration
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

#include "utils.h"
#include "queue.h"
#include "tray.h"
#include "capture.h"
#include "encoder.h"
#include "muxer.h"

// Constants
constexpr int TARGET_WIDTH = 1920;
constexpr int TARGET_HEIGHT = 1080;
constexpr int TARGET_FPS = 30;
constexpr int TARGET_BITRATE = 5000; // 5 Mbps
constexpr size_t FRAME_QUEUE_SIZE = 4;

// Frame data for queue (simplified - owns pixel data)
struct FrameData {
    std::vector<uint8_t> pixels;
    int width;
    int height;
    int linesize;
    int64_t frameIndex;
    
    FrameData() : width(0), height(0), linesize(0), frameIndex(0) {}
};

// Global state
static std::atomic<bool> g_isRecording{false};
static std::atomic<bool> g_shouldStop{false};
static std::atomic<int64_t> g_frameCount{0};

static SystemTray g_tray;
static HotkeyManager g_hotkeys;
static std::unique_ptr<ScreenCapture> g_capture;
static std::unique_ptr<H264Encoder> g_encoder;
static std::unique_ptr<MP4Muxer> g_muxer;

// Lock-free queue for frame transfer
static LockFreeSPSCQueue<FrameData, FRAME_QUEUE_SIZE> g_frameQueue;

// Preallocated frame pool to avoid allocations during recording
static std::vector<std::vector<uint8_t>> g_framePool;
static std::atomic<size_t> g_poolIndex{0};

// Recording thread
static HANDLE g_captureThread = nullptr;
static HANDLE g_encodeThread = nullptr;

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            // Show context menu on right-click
            POINT pt;
            GetCursorPos(&pt);
            
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_START_STOP, 
                       g_isRecording ? L"Stop Recording" : L"Start Recording");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
            
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                          pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            // Double-click toggles recording
            if (g_onStartStop) {
                g_onStartStop();
            }
        }
        return 0;
        
    case WM_HOTKEY:
        if (wParam == HOTKEY_START_STOP) {
            if (g_onStartStop) {
                g_onStartStop();
            }
        }
        return 0;
        
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Capture thread function
DWORD WINAPI CaptureThreadProc(LPVOID lpParam) {
    (void)lpParam;
    
    LOG_DEBUG("Capture thread started");
    
    while (!g_shouldStop.load()) {
        if (!g_isRecording.load()) {
            Sleep(10);
            continue;
        }
        
        CapturedFrame capturedFrame;
        if (!g_capture->CaptureFrame(capturedFrame)) {
            Sleep(1); // Brief sleep if no frame available
            continue;
        }
        
        // Get preallocated buffer from pool
        size_t poolIdx = g_poolIndex.fetch_add(1, std::memory_order_relaxed) % g_framePool.size();
        auto& buffer = g_framePool[poolIdx];
        
        // Calculate required size
        size_t requiredSize = static_cast<size_t>(capturedFrame.height * capturedFrame.linesize[0]);
        if (buffer.size() < requiredSize) {
            buffer.resize(requiredSize);
        }
        
        // Copy frame data
        memcpy(buffer.data(), capturedFrame.data[0], requiredSize);
        
        // Prepare frame for queue
        FrameData frame;
        frame.pixels.swap(buffer);  // Transfer ownership
        frame.width = capturedFrame.width;
        frame.height = capturedFrame.height;
        frame.linesize = capturedFrame.linesize[0];
        frame.frameIndex = g_frameCount.fetch_add(1, std::memory_order_relaxed);
        
        // Push to queue (drops oldest if full)
        g_frameQueue.push(std::move(frame));
        
        // Release capture resources
        g_capture->ReleaseFrame(capturedFrame);
        
        // Maintain ~30fps timing
        Sleep(33);
    }
    
    LOG_DEBUG("Capture thread stopped");
    return 0;
}

// Encode/Mux thread function
DWORD WINAPI EncodeThreadProc(LPVOID lpParam) {
    (void)lpParam;
    
    LOG_DEBUG("Encode thread started");
    
    while (!g_shouldStop.load() || !g_frameQueue.empty()) {
        if (!g_isRecording.load() && g_frameQueue.empty()) {
            Sleep(10);
            continue;
        }
        
        FrameData frame;
        if (!g_frameQueue.pop(frame)) {
            if (!g_isRecording.load()) {
                break; // No more frames and not recording
            }
            Sleep(1);
            continue;
        }
        
        // Encode frame
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            continue;
        }
        
        bool isKeyframe = false;
        int encodedSize = g_encoder->EncodeFrame(
            frame.pixels.data(), 
            frame.linesize,
            packet,
            &isKeyframe
        );
        
        if (encodedSize > 0) {
            // Write to muxer
            g_muxer->WritePacket(packet, frame.frameIndex);
        }
        
        av_packet_free(&packet);
        
        // Return buffer to pool
        size_t poolIdx = g_poolIndex.load(std::memory_order_relaxed) % g_framePool.size();
        g_framePool[poolIdx].swap(frame.pixels);
    }
    
    // Flush encoder
    AVPacket* flushPacket = av_packet_alloc();
    while (flushPacket && g_encoder->FlushEncoder(flushPacket) > 0) {
        g_muxer->WritePacket(flushPacket, g_frameCount.load());
        av_packet_unref(flushPacket);
    }
    if (flushPacket) {
        av_packet_free(&flushPacket);
    }
    
    LOG_DEBUG("Encode thread stopped");
    return 0;
}

// Start/stop recording callback
void OnStartStop() {
    if (g_isRecording.load()) {
        // Stop recording
        g_isRecording.store(false);
        g_tray.UpdateTooltip(false);
        g_tray.ShowNotification(L"Recording Stopped", L"Video saved to Videos\\Recordings");
        LOG_DEBUG("Recording stopped");
    } else {
        // Start recording
        g_frameCount.store(0);
        g_poolIndex.store(0);
        
        // Initialize components
        g_capture = ScreenCapture::Create();
        if (!g_capture->Initialize(TARGET_WIDTH, TARGET_HEIGHT, TARGET_FPS)) {
            g_tray.ShowNotification(L"Error", L"Failed to initialize capture");
            return;
        }
        
        EncoderConfig encConfig;
        encConfig.width = g_capture->Width();
        encConfig.height = g_capture->Height();
        encConfig.fps = TARGET_FPS;
        encConfig.bitrate = TARGET_BITRATE;
        encConfig.useQSV = true;  // Try QSV first
        encConfig.profile = "main";
        
        g_encoder = std::make_unique<H264Encoder>();
        if (!g_encoder->Initialize(encConfig)) {
            g_tray.ShowNotification(L"Error", L"Failed to initialize encoder");
            return;
        }
        
        std::wstring outputPath = GenerateOutputFilename();
        g_muxer = std::make_unique<MP4Muxer>();
        if (!g_muxer->Initialize(outputPath, g_encoder->GetCodecContext())) {
            g_tray.ShowNotification(L"Error", L"Failed to initialize muxer");
            return;
        }
        
        g_isRecording.store(true);
        g_tray.UpdateTooltip(true);
        g_tray.ShowNotification(L"Recording Started", L"Press F10 or double-click tray icon to stop");
        LOG_DEBUG("Recording started: %ls", outputPath.c_str());
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    
    // Initialize FFmpeg
    av_log_set_level(AV_LOG_ERROR);
    avcodec_register_all();
    avformat_network_init();
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"ScreenRecorderClass";
    
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(wcex);
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    if (!RegisterClassExW(&wcex)) {
        LogError("Failed to register window class");
        return 1;
    }
    
    // Create hidden message window
    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Screen Recorder",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, hInstance, nullptr
    );
    
    if (!hwnd) {
        LogError("Failed to create window");
        return 1;
    }
    
    // Initialize tray
    UINT taskId = RegisterWindowMessageW(L"ScreenRecorderTaskbarCreated");
    if (!g_tray.Initialize(hwnd, taskId)) {
        LogError("Failed to initialize system tray");
        return 1;
    }
    
    // Register hotkeys (F9 = start, F10 = stop - same key toggles)
    if (!g_hotkeys.Register(hwnd, HOTKEY_START_STOP, 0, VK_F9)) {
        LogError("Failed to register F9 hotkey");
    }
    if (!g_hotkeys.Register(hwnd, HOTKEY_START_STOP + 1, 0, VK_F10)) {
        LogError("Failed to register F10 hotkey");
    }
    
    // Set global callback
    g_onStartStop = OnStartStop;
    
    // Preallocate frame pool (4 frames x max 1920x1080x4 bytes)
    const size_t frameSize = TARGET_WIDTH * TARGET_HEIGHT * 4;
    g_framePool.resize(FRAME_QUEUE_SIZE);
    for (auto& buf : g_framePool) {
        buf.resize(frameSize);
    }
    
    // Start capture and encode threads
    g_captureThread = CreateThread(nullptr, 0, CaptureThreadProc, nullptr, 0, nullptr);
    g_encodeThread = CreateThread(nullptr, 0, EncodeThreadProc, nullptr, 0, nullptr);
    
    if (!g_captureThread || !g_encodeThread) {
        LogError("Failed to create worker threads");
        return 1;
    }
    
    // Message loop
    MSG msg;
    bool running = true;
    
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            
            // Handle taskbar recreation (theme change, explorer restart)
            if (msg.message == taskId) {
                g_tray.Cleanup();
                g_tray.Initialize(hwnd, taskId);
            }
            
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        // Small sleep to reduce CPU usage
        Sleep(10);
    }
    
    // Cleanup
    g_shouldStop.store(true);
    
    // Wait for threads to finish
    if (g_captureThread) {
        WaitForSingleObject(g_captureThread, 2000);
        CloseHandle(g_captureThread);
    }
    if (g_encodeThread) {
        WaitForSingleObject(g_encodeThread, 2000);
        CloseHandle(g_encodeThread);
    }
    
    // Finalize muxer if recording
    if (g_muxer && g_muxer->IsInitialized()) {
        g_muxer->Finalize();
    }
    
    // Cleanup COM (if initialized by DXGI)
    CoUninitialize();
    
    g_hotkeys.UnregisterAll();
    g_tray.Cleanup();
    
    UnregisterClassW(CLASS_NAME, hInstance);
    
    return 0;
}
