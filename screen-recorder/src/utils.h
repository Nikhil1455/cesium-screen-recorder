// utils.h - Utility functions and RAII wrappers
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <memory>
#include <cstdio>

// RAII wrapper for HGLOBAL
struct HGlobalDeleter {
    void operator()(HGLOBAL h) const {
        if (h) GlobalFree(h);
    }
};
using HGlobalPtr = std::unique_ptr<std::remove_pointer_t<HGLOBAL>, HGlobalDeleter>;

// RAII wrapper for HDC
struct HDCDeleter {
    void operator()(HDC hdc) const {
        if (hdc) DeleteDC(hdc);
    }
};
using HDCPtr = std::unique_ptr<std::remove_pointer_t<HDC>, HDCDeleter>;

// RAII wrapper for HBITMAP
struct HBITMAPDeleter {
    void operator()(HBITMAP bmp) const {
        if (bmp) DeleteObject(bmp);
    }
};
using HBITMAPPTR = std::unique_ptr<std::remove_pointer_t<HBITMAP>, HBITMAPDeleter>;

// Get known folder path (e.g., FOLDERID_Videos)
inline std::wstring GetKnownFolderPath(const GUID& folderId) {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &path);
    if (SUCCEEDED(hr)) {
        std::wstring result(path);
        CoTaskMemFree(path);
        return result;
    }
    return L"";
}

// Create directory if it doesn't exist
inline bool CreateDirectoryRecursive(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    return CreateDirectoryW(path.c_str(), nullptr) || 
           GetLastError() == ERROR_ALREADY_EXISTS;
}

// Get output directory for recordings
inline std::wstring GetRecordingDirectory() {
    std::wstring videosPath = GetKnownFolderPath(FOLDERID_Videos);
    if (videosPath.empty()) {
        // Fallback to user profile
        wchar_t userProfile[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
            videosPath = std::wstring(userProfile) + L"\\Videos";
        } else {
            videosPath = L"C:\\Users\\Public\\Videos";
        }
    }
    
    std::wstring recordingsPath = videosPath + L"\\Recordings";
    CreateDirectoryRecursive(recordingsPath);
    return recordingsPath;
}

// Generate timestamp-based filename
inline std::wstring GenerateOutputFilename() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    wchar_t filename[128];
    swprintf_s(filename, L"Recording_%04d%02d%02d_%02d%02d%02d.mp4",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    
    return GetRecordingDirectory() + L"\\" + filename;
}

// Debug logging (disabled in release builds)
#ifdef _DEBUG
#define LOG_DEBUG(fmt, ...) do { \
    fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
} while(0)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

// Error logging
inline void LogError(const char* msg) {
    fprintf(stderr, "[ERROR] %s\n", msg);
}

// HRESULT to string helper
inline const char* HResultToString(HRESULT hr) {
    switch (hr) {
        case S_OK: return "S_OK";
        case E_FAIL: return "E_FAIL";
        case E_INVALIDARG: return "E_INVALIDARG";
        case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
        case DXGI_ERROR_ACCESS_LOST: return "DXGI_ERROR_ACCESS_LOST";
        case DXGI_ERROR_WAIT_TIMEOUT: return "DXGI_ERROR_WAIT_TIMEOUT";
        default: return "Unknown";
    }
}

// MinGW-specific: Define missing constants if needed
#ifndef D3D11_CREATE_DEVICE_VIDEO_SUPPORT
#define D3D11_CREATE_DEVICE_VIDEO_SUPPORT 0x00000800
#endif
