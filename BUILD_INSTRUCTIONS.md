# Build Instructions for Windows Screen Recorder

## Prerequisites

### 1. Visual Studio 2019/2022 (MSVC)
- Download: https://visualstudio.microsoft.com/downloads/
- Install "Desktop development with C++" workload
- Ensure MSVC v142+ toolset is installed

### 2. CMake 3.15+
- Download: https://cmake.org/download/
- Or install via winget: `winget install Kitware.CMake`

### 3. FFmpeg with QSV Support (CRITICAL)

**Recommended: BtbN FFmpeg Builds with QSV**

Download from: https://github.com/BtbN/FFmpeg-Builds/releases

Choose one of these builds (must include QSV):
- **Shared build**: `ffmpeg-master-latest-win64-gpl-shared.zip`
- **Static build**: `ffmpeg-master-latest-win64-gpl-static.zip`

**Verify QSV support after download:**
```cmd
cd <ffmpeg_extracted_folder>\bin
ffmpeg -hide_banner -encoders | findstr qsv
```

Expected output should include:
```
 V..... h264_qsv           H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (codec h264)
 V..... hevc_qsv           HEVC (codec hevc)
```

If you don't see `h264_qsv`, the build doesn't have QSV support!

**Alternative FFmpeg builds with QSV:**
- Gyan.dev builds: https://www.gyan.dev/ffmpeg/builds/ (choose "full" version)
- Official FFmpeg with MediaSDK: Requires building from source with Intel Media SDK

## Build Steps

### Step 1: Extract FFmpeg
Extract the downloaded FFmpeg archive to a permanent location, e.g.:
```
C:\dev\ffmpeg
```

The directory structure should look like:
```
C:\dev\ffmpeg\
├── bin\
│   ├── avcodec-*.dll
│   ├── avformat-*.dll
│   ├── avutil-*.dll
│   ├── swscale-*.dll
│   └── ffmpeg.exe
├── include\
│   ├── libavcodec\
│   ├── libavformat\
│   ├── libavutil\
│   └── libswscale\
└── lib\
    ├── avcodec.lib
    ├── avformat.lib
    ├── avutil.lib
    └── swscale.lib
```

### Step 2: Set Environment Variable
```cmd
setx FFMPEG_ROOT "C:\dev\ffmpeg"
```

Or pass it directly to CMake in the next step.

### Step 3: Create Build Directory
```cmd
cd C:\path\to\screen_recorder
mkdir build
cd build
```

### Step 4: Configure with CMake

**Option A: Using FFMPEG_ROOT environment variable**
```cmd
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
```

**Option B: Passing FFMPEG_ROOT directly**
```cmd
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DFFMPEG_ROOT="C:\dev\ffmpeg"
```

### Step 5: Build
```cmd
cmake --build . --config Release
```

The executable will be at: `build\Release\ScreenRecorder.exe`

### Step 6: Copy FFmpeg DLLs (if using shared build)
```cmd
copy C:\dev\ffmpeg\bin\*.dll Release\
```

Required DLLs:
- avcodec-*.dll
- avformat-*.dll
- avutil-*.dll
- swscale-*.dll
- swresample-*.dll

## Alternative: Manual Build without CMake

Create a Visual Studio project with these settings:

### Project Properties
- **Platform**: x64
- **Configuration**: Release
- **C++ Language Standard**: C++17
- **Runtime Library**: Multi-threaded (/MT)

### Additional Include Directories
```
$(ProjectDir)include
C:\dev\ffmpeg\include
```

### Additional Library Directories
```
C:\dev\ffmpeg\lib
```

### Additional Dependencies
```
avcodec.lib
avformat.lib
avutil.lib
swscale.lib
swresample.lib
d3d11.lib
dxgi.lib
dwmapi.lib
```

### Preprocessor Definitions
```
_CRT_SECURE_NO_WARNINGS
NOMINMAX
WIN32_LEAN_AND_MEAN
HAS_QSV_SUPPORT
```

### Compiler Flags (Command Line)
```
/O2 /arch:AVX2 /GL /Gy /Oi /MP /Zc:inline /Qpar
```

### Linker Flags (Command Line)
```
/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO
```

## Usage

### Basic Usage
```cmd
ScreenRecorder.exe
```

Default settings: 1920x1080 @ 30fps, 4000kbps, QSV encoder

### Command Line Options
```cmd
ScreenRecorder.exe --help

Options:
  --width <pixels>     Output width (default: 1920)
  --height <pixels>    Output height (default: 1080)
  --fps <fps>          Frame rate (default: 30 or 60)
  --bitrate <kbps>     Bitrate in kbps (default: 4000)
  --encoder <type>     Encoder: qsv, x264, auto (default: auto)
  --profile <profile>  H.264 profile: baseline, main (default: main)
  --output <path>      Output file path (default: recording.mp4)
  --monitor <index>    Monitor index for multi-monitor (default: 0)
  --gdi                Force GDI capture (Win7 fallback)
```

### Examples

**1080p60 with QSV:**
```cmd
ScreenRecorder.exe --width 1920 --height 1080 --fps 60 --bitrate 6000 --encoder qsv
```

**720p30 for minimal CPU usage:**
```cmd
ScreenRecorder.exe --width 1280 --height 720 --fps 30 --bitrate 2500
```

**Force x264 software encoder:**
```cmd
ScreenRecorder.exe --encoder x264
```

**Multi-monitor setup (second monitor):**
```cmd
ScreenRecorder.exe --monitor 1
```

### Config File (config.txt)
Create a `config.txt` in the same directory as the executable:

```ini
# Screen Recorder Configuration
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

### Verify QSV is Active
```cmd
ffmpeg -hide_banner -encoders | findstr qsv
```

Look for `h264_qsv` in the output.

### Check Thread Affinity
Use Task Manager or Process Explorer:
1. Open Task Manager → Details tab
2. Right-click ScreenRecorder.exe → Set affinity
3. Verify two threads: one on CPU 0, one on CPU 1

### Monitor Performance
During recording, the console shows:
```
Performance: Capture=30.0 fps, Encode=30.0 fps, Dropped=0, Queue=1
```

### Expected Performance on i5-5300U (Intel HD 5500)

| Resolution | FPS | Encoder | CPU Usage | RAM Usage | Notes |
|------------|-----|---------|-----------|-----------|-------|
| 1920x1080 | 30 | QSV | 15-25% | ~50MB | Optimal |
| 1920x1080 | 60 | QSV | 25-40% | ~55MB | Good |
| 1280x720 | 30 | QSV | 10-15% | ~45MB | Very light |
| 1920x1080 | 30 | x264 | 60-90% | ~50MB | Heavy! |
| 1280x720 | 30 | x264 | 40-60% | ~45MB | Manageable |

## Windows Version Behavior

### Windows 8/10/11
- Uses DXGI Desktop Duplication API
- GPU-resident frame capture (zero-copy when possible)
- Lower CPU overhead, better performance

### Windows 7
- Falls back to GDI BitBlt automatically
- Higher CPU usage expected (~20-30% more than DXGI)
- Double-buffering reduces flicker
- 600ms sleep if encode queue backs up

## Game Compatibility Tips

### Recommended Settings
1. **Borderless Windowed Mode**: Strongly recommended for all games
   - DXGI works best with borderless windowed
   - Full-screen exclusive may cause capture failures

2. **Disable V-Sync in game**: Let the recorder handle frame timing

3. **Run as Administrator**: Required for some games with anti-cheat

4. **Game-specific notes**:
   - **DirectX 11/12 games**: Work best with DXGI
   - **OpenGL games**: May require GDI fallback
   - **Vulkan games**: Generally compatible with DXGI
   - **Unity games**: Use borderless windowed mode

### Troubleshooting

**Black screen in recording:**
- Try running as administrator
- Switch to borderless windowed mode
- Use `--gdi` flag as fallback

**High CPU usage:**
- Lower resolution (--width 1280 --height 720)
- Reduce FPS (--fps 30)
- Ensure QSV is active (check logs)

**Dropped frames:**
- Close background applications
- Reduce bitrate
- Use SSD for output file

**QSV not detected:**
- Verify FFmpeg build has QSV support
- Update Intel graphics drivers
- Check BIOS for Quick Sync enablement

## Memory Optimization

The recorder uses fixed-size preallocated pools:
- Maximum 4 frames in buffer pool
- Lock-free SPSC ring buffer
- Zero malloc/free during recording loop
- Target RAM usage: <60MB

## License Notes

- FFmpeg GPL builds require your application to be GPL if distributed
- For proprietary use, consider LGPL FFmpeg builds
- This code is provided as-is for educational purposes
