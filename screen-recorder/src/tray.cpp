// tray.cpp - System tray icon and hotkey implementation
#include "tray.h"
#include "utils.h"

// Global callback
StartStopCallback g_onStartStop;

// Simple icon data (16x16 red/green circle encoded as minimal ICO)
static const unsigned char s_iconData[] = {
    0x00,0x00,0x01,0x00,0x01,0x00,0x10,0x10,0x02,0x00,0x00,0x00,0x00,0x00,
    0x28,0x01,0x00,0x00,0x16,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x10,0x00,
    0x00,0x00,0x01,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x12,0x01,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,
    0x00,0x00,0xff,0x00,0x00,0x00,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // ... truncated for brevity, will use stock icon instead
};

SystemTray::SystemTray() : hwnd_(nullptr), taskId_(0), initialized_(false) {
    ZeroMemory(&nid_, sizeof(nid_));
}

SystemTray::~SystemTray() {
    Cleanup();
}

bool SystemTray::Initialize(HWND hwnd, UINT taskId) {
    hwnd_ = hwnd;
    taskId_ = taskId;

    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid_.uCallbackMessage = WM_TRAYICON;
    
    // Use stock application icon
    nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (!nid_.hIcon) {
        LogError("Failed to load stock icon");
        return false;
    }

    wcscpy_s(nid_.szTip, L"Screen Recorder - Press F9 to start");
    
    initialized_ = Shell_NotifyIconW(NIM_ADD, &nid_);
    return initialized_;
}

void SystemTray::ShowNotification(const wchar_t* title, const wchar_t* message) {
    if (!initialized_) return;

    nid_.uFlags |= NIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid_.szInfo, message, _TRUNCATE);
    nid_.dwInfoFlags = NIIF_INFO;
    nid_.uTimeout = 3000;

    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    
    // Reset flags
    nid_.uFlags &= ~NIF_INFO;
}

void SystemTray::UpdateTooltip(bool isRecording) {
    if (!initialized_) return;

    if (isRecording) {
        wcscpy_s(nid_.szTip, L"Screen Recorder - Recording... (F10 to stop)");
    } else {
        wcscpy_s(nid_.szTip, L"Screen Recorder - Press F9 to start");
    }
    
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void SystemTray::Cleanup() {
    if (initialized_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        initialized_ = false;
    }
}

// HotkeyManager implementation
HotkeyManager::HotkeyManager() : hotkeyCount_(0) {
    ZeroMemory(hotkeys_, sizeof(hotkeys_));
}

HotkeyManager::~HotkeyManager() {
    UnregisterAll();
}

bool HotkeyManager::Register(HWND hwnd, UINT id, UINT modifiers, UINT vk) {
    if (hotkeyCount_ >= _countof(hotkeys_)) {
        return false;
    }

    if (!::RegisterHotKey(hwnd, id, modifiers, vk)) {
        LogError("Failed to register hotkey");
        return false;
    }

    hotkeys_[hotkeyCount_] = {id, modifiers, vk};
    hotkeyCount_++;
    return true;
}

void HotkeyManager::Unregister(UINT id) {
    ::UnregisterHotKey(nullptr, id);
    
    for (size_t i = 0; i < hotkeyCount_; i++) {
        if (hotkeys_[i].id == id) {
            // Shift remaining hotkeys
            for (size_t j = i; j < hotkeyCount_ - 1; j++) {
                hotkeys_[j] = hotkeys_[j + 1];
            }
            hotkeyCount_--;
            break;
        }
    }
}

void HotkeyManager::UnregisterAll() {
    for (size_t i = 0; i < hotkeyCount_; i++) {
        ::UnregisterHotKey(nullptr, hotkeys_[i].id);
    }
    hotkeyCount_ = 0;
}
