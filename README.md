# Ultra-Low-Overhead Windows Screen Recorder

Production-ready screen recording application optimized for **Intel Broadwell dual-core CPUs (i5-5300U)** with **Intel HD 5500 integrated graphics**. Targets Windows 7, 8, 10, and 11 on x86_64.

## Key Features

### Hardware-Specific Optimizations
- **CPU Thread Isolation**: 
  - Capture thread pinned to Core 0 with `THREAD_PRIORITY_TIME_CRITICAL`
  - Encode/Mux thread pinned to Core 1 with `THREAD_PRIORITY_BELOW_NORMAL`
  
- **GPU Acceleration**:
  - Primary: Intel Quick Sync Video (QSV) via `h264_qsv` encoder
  - Fallback: x264 with `ultrafast` preset and `zerolatency` tune
  
- **Memory Efficiency**:
  - Fixed 4-frame preallocated pool
  - Lock-free SPSC ring buffer with atomic indices
  - Zero malloc/free during recording loop
  - Target RAM usage: <60MB

### Capture Methods
- **Windows 8/10/11**: DXGI Desktop Duplication API
  - GPU-resident frames, async capture
  - Skips vsync synchronization to avoid stutter
  
- **Windows 7**: GDI BitBlt with double-buffering
  - 600ms sleep if encode queue >2 frames
  - Captures BGRA only (alpha channel skipped)

### Color Conversion
- Primary: FFmpeg swscale with AVX2-optimized kernels (`SWS_FAST_BILINEAR`)
- QSV VPP available but uses swscale for simplicity

### Output
- Container: MP4 with `+faststart` flag (moov atom at beginning)
- Codec: H.264 Baseline/Main profile
- Resolution: Configurable (default 1920x1080)
- Frame Rate: 30 or 60 FPS (CFR output)
- Bitrate: Configurable (default 4000 kbps)

## Project Structure

```
screen_recorder/
├── CMakeLists.txt          # Build configuration
├── BUILD_INSTRUCTIONS.md   # Detailed build guide
├── config.txt              # Runtime configuration template
├── include/
│   ├── ring_buffer.h       # Lock-free SPSC queue + frame pool
│   ├── capture_dxgi.h      # DXGI Desktop Duplication capture
│   ├── capture_gdi.h       # GDI BitBlt capture (Win7)
│   ├── encoder_qsv.h       # QSV/x264 encoder abstraction
│   ├── muxer.h             # MP4 muxer
│   └── utils.h             # Logging, RAII, thread helpers
└── src/
    ├── main.cpp            # Main entry point + threading
    ├── capture_dxgi.cpp    # DXGI implementation
    ├── capture_gdi.cpp     # GDI implementation
    ├── encoder_qsv.cpp     # QSV/x264 encoder implementation
    └── muxer.cpp           # MP4 muxer implementation
```

## Quick Start

### Prerequisites
1. Visual Studio 2019/2022 with C++ desktop workload
2. CMake 3.15+
3. FFmpeg with QSV support (see BUILD_INSTRUCTIONS.md)

### Build
```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DFFMPEG_ROOT="C:\dev\ffmpeg"
cmake --build . --config Release
```

### Run
```cmd
ScreenRecorder.exe --width 1920 --height 1080 --fps 30 --bitrate 4000
```

Press `Ctrl+C` to stop recording.

## Configuration

### Command Line Options
```
--width <pixels>     Output width (default: 1920)
--height <pixels>    Output height (default: 1080)
--fps <fps>          Frame rate (default: 30)
--bitrate <kbps>     Bitrate in kbps (default: 4000)
--encoder <type>     Encoder: qsv, x264, auto (default: auto)
--profile <profile>  H.264 profile: baseline, main (default: main)
--output <path>      Output file path (default: recording.mp4)
--monitor <index>    Monitor index (default: 0)
--gdi                Force GDI capture (Win7 fallback)
```

### Config File (config.txt)
```ini
width 1920
height 1080
fps 30
bitrate 4000
encoder qsv
profile main
output recording.mp4
monitor 0
use_gdi false
```

## Performance Verification

### Verify QSV Support
```cmd
ffmpeg -hide_banner -encoders | findstr qsv
```

Expected output:
```
V..... h264_qsv    H.264 / AVC encoder (codec h264)
V..... hevc_qsv    HEVC encoder (codec hevc)
```

### Expected Performance (i5-5300U @ 1080p30)

| Encoder | CPU Usage | RAM | Notes |
|---------|-----------|-----|-------|
| QSV | 15-25% | ~50MB | Optimal |
| x264 | 60-90% | ~50MB | Heavy load! |

## Architecture Details

### Threading Model
```
┌─────────────────┐     ┌──────────────────────┐
│  Capture Thread │     │  Encode/Mux Thread   │
│    (Core 0)     │     │      (Core 1)        │
│ TIME_CRITICAL   │────▶│  BELOW_NORMAL        │
│                 │     │                      │
│ • DXGI/GDI      │     │ • Color Convert      │
│ • Frame capture │     │ • QSV/x264 Encode    │
│                 │     │ • MP4 Mux            │
└─────────────────┘     └──────────────────────┘
         │                        │
         ▼                        ▼
┌─────────────────────────────────────────┐
│     Lock-Free SPSC Ring Buffer          │
│     (4-frame capacity, atomic ops)      │
└─────────────────────────────────────────┘
```

### Memory Layout
- **Frame Pool**: 4 × preallocated BGRA buffers (1920×1080 × 4 bytes each)
- **NV12 Pool**: 4 × preallocated NV12 buffers (Y + UV planes)
- **Ring Buffer**: Cache-line aligned atomic indices (64-byte padding)

### Code Standards
- ✅ RAII for all FFmpeg contexts, DXGI resources, GDI handles
- ✅ Zero dynamic allocation during recording loop
- ✅ Explicit HRESULT/AVERROR handling with retry/backoff
- ✅ Minimal logging (stdout, no GUI)
- ✅ Compile-time checks for AVX2, DXGI version, QSV availability

## Game Compatibility

### Recommended Settings
1. **Borderless Windowed Mode** (strongly recommended)
2. Disable in-game V-Sync
3. Run as Administrator for anti-cheat compatibility

### Known Working Games
- DirectX 11/12 titles (DXGI)
- Vulkan games (DXGI)
- OpenGL games (may require GDI fallback)
- Unity/Unreal Engine games

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Black screen | Run as admin, use borderless windowed, try `--gdi` |
| High CPU usage | Lower resolution/FPS, ensure QSV is active |
| Dropped frames | Close background apps, reduce bitrate |
| QSV not detected | Update Intel graphics drivers, check FFmpeg build |

## License

This project uses FFmpeg. If distributing binaries:
- GPL FFmpeg builds require your application to be GPL-licensed
- Consider LGPL FFmpeg builds for proprietary distribution

## Credits

Optimized for Intel Broadwell (5th Gen Core) with Intel HD Graphics 5500.
