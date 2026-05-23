// tray.h - System tray icon and hotkey management
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <functional>

class SystemTray {
public:
    SystemTray();
    ~SystemTray();

    bool Initialize(HWND hwnd, UINT taskId);
    void ShowNotification(const wchar_t* title, const wchar_t* message);
    void UpdateTooltip(bool isRecording);
    void Cleanup();

private:
    NOTIFYICONDATAW nid_;
    HWND hwnd_;
    UINT taskId_;
    bool initialized_;
};

class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    bool Register(HWND hwnd, UINT id, UINT modifiers, UINT vk);
    void Unregister(UINT id);
    void UnregisterAll();

private:
    struct RegisteredHotkey {
        UINT id;
        UINT modifiers;
        UINT vk;
    };
    
    RegisteredHotkey hotkeys_[4];
    size_t hotkeyCount_;
};

// Global callbacks
using StartStopCallback = std::function<void()>;
extern StartStopCallback g_onStartStop;

// Tray command IDs
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr UINT ID_TRAY_START_STOP = 1002;

// Hotkey IDs
constexpr UINT HOTKEY_START_STOP = 1;
