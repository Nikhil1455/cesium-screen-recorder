#pragma once

#include <Windows.h>
#include <string>
#include <chrono>
#include <cstdio>

// Minimal logging utility - no GUI, stdout or rolling log file
namespace Logger {
    enum class Level {
        INFO,
        WARNING,
        ERROR,
        DEBUG
    };

    inline Level current_level = Level::INFO;

    inline void set_log_level(Level level) {
        current_level = level;
    }

    inline const char* level_to_string(Level level) {
        switch (level) {
            case Level::INFO: return "INFO";
            case Level::WARNING: return "WARN";
            case Level::ERROR: return "ERROR";
            case Level::DEBUG: return "DEBUG";
            default: return "UNKNOWN";
        }
    }

    inline void log(Level level, const char* format, ...) {
        if (level < current_level) return;

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        char timestamp[64];
#ifdef _MSC_VER
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&time_t_now));
#else
        struct tm tm_buf;
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime_r(&time_t_now, &tm_buf));
#endif

        fprintf(stdout, "[%s.%03lld] [%s] ", timestamp, (long long)ms.count(), level_to_string(level));

        va_list args;
        va_start(args, format);
        vfprintf(stdout, format, args);
        va_end(args);

        fprintf(stdout, "\n");
        fflush(stdout);
    }

#define LOG_INFO(...) Logger::log(Logger::Level::INFO, __VA_ARGS__)
#define LOG_WARN(...) Logger::log(Logger::Level::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) Logger::log(Logger::Level::ERROR, __VA_ARGS__)
#ifdef _DEBUG
#define LOG_DEBUG(...) Logger::log(Logger::Level::DEBUG, __VA_ARGS__)
#else
#define LOG_DEBUG(...) do {} while(0)
#endif
}

// HRESULT error handling helper
inline bool check_hresult(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        Logger::log(Logger::Level::ERROR, "%s failed with HRESULT 0x%08lX", operation, hr);
        return false;
    }
    return true;
}

// FFmpeg error handling helper
inline bool check_av_error(int err, const char* operation) {
    if (err < 0) {
        char err_buf[256];
        av_strerror(err, err_buf, sizeof(err_buf));
        Logger::log(Logger::Level::ERROR, "%s failed: %s (code %d)", operation, err_buf, err);
        return false;
    }
    return true;
}

// RAII wrapper for Windows handles
template<typename T, typename Deleter>
class RAIIWrapper {
public:
    RAIIWrapper() : handle_(T{}) {}
    explicit RAIIWrapper(T handle, Deleter deleter) : handle_(handle), deleter_(deleter) {}
    
    ~RAIIWrapper() {
        if (handle_ != T{}) {
            deleter_(handle_);
        }
    }
    
    // Prevent copying
    RAIIWrapper(const RAIIWrapper&) = delete;
    RAIIWrapper& operator=(const RAIIWrapper&) = delete;
    
    // Allow moving
    RAIIWrapper(RAIIWrapper&& other) noexcept 
        : handle_(other.handle_), deleter_(other.deleter_) {
        other.handle_ = T{};
    }
    
    RAIIWrapper& operator=(RAIIWrapper&& other) noexcept {
        if (this != &other) {
            if (handle_ != T{}) {
                deleter_(handle_);
            }
            handle_ = other.handle_;
            deleter_ = other.deleter_;
            other.handle_ = T{};
        }
        return *this;
    }
    
    T get() const { return handle_; }
    T* ptr() { return &handle_; }
    bool valid() const { return handle_ != T{}; }
    
    void reset(T handle = T{}) {
        if (handle_ != T{}) {
            deleter_(handle_);
        }
        handle_ = handle;
    }

private:
    T handle_;
    Deleter deleter_;
};

// Thread affinity helper for Broadwell dual-core optimization
inline bool set_thread_affinity(HANDLE thread, DWORD_PTR core_mask) {
    DWORD_PTR result = SetThreadAffinityMask(thread, core_mask);
    if (result == 0) {
        Logger::log(Logger::Level::WARNING, "Failed to set thread affinity: %lu", GetLastError());
        return false;
    }
    return true;
}

// Thread priority helper
inline bool set_thread_priority(HANDLE thread, int priority) {
    if (!SetThreadPriority(thread, priority)) {
        Logger::log(Logger::Level::WARNING, "Failed to set thread priority: %lu", GetLastError());
        return false;
    }
    return true;
}

// Performance counter for timing
class PerformanceTimer {
public:
    PerformanceTimer() {
        QueryPerformanceFrequency(&frequency_);
        QueryPerformanceCounter(&start_);
    }
    
    double elapsed_ms() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (now.QuadPart - start_.QuadPart) * 1000.0 / frequency_.QuadPart;
    }
    
    void reset() {
        QueryPerformanceCounter(&start_);
    }

private:
    LARGE_INTEGER start_;
    LARGE_INTEGER frequency_;
};

// Detect Windows version
inline int get_windows_version() {
    OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    
    // Use RtlGetVersion for accurate detection (works on Win10/11)
    using RtlGetVersionPtr = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE hntdll = GetModuleHandleW(L"ntdll.dll");
    if (hntdll) {
        auto rtl_get_version = reinterpret_cast<RtlGetVersionPtr>(
            GetProcAddress(hntdll, "RtlGetVersion"));
        if (rtl_get_version) {
            RTL_OSVERSIONINFOW rovi = {};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (rtl_get_version(&rovi) == 0) {
                if (rovi.dwMajorVersion >= 10 && rovi.dwBuildNumber >= 22000) {
                    return 11;  // Windows 11
                } else if (rovi.dwMajorVersion >= 10) {
                    return 10;  // Windows 10
                } else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 3) {
                    return 8;   // Windows 8.1
                } else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 2) {
                    return 8;   // Windows 8
                } else if (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion == 1) {
                    return 7;   // Windows 7
                }
            }
        }
    }
    
    // Fallback (inaccurate on Win10/11 due to GetVersionEx deprecation)
    return 10;
}

// Check if running on Intel HD 5500 (Broadwell)
inline bool is_intel_broadwell() {
    // This is a simplified check - in production you'd use CPUID
    // For now, we assume the target hardware based on user configuration
    return true;  // Optimized for Broadwell by default
}
