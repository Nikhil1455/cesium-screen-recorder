# Step-by-Step VS Code Build Guide

This guide walks you through building the Screen Recorder on Windows using VS Code and MinGW-w64.

## Prerequisites Installation

### 1. Install MSYS2

1. Download MSYS2 from https://www.msys2.org/
2. Run the installer (accept defaults)
3. After installation, the MSYS2 UCRT64 terminal will open automatically

### 2. Install Required Packages

In the MSYS2 terminal, run:

```bash
# Update package database
pacman -Syu

# Close terminal when prompted, reopen MSYS2 UCRT64

# Install toolchain and build tools
pacman -S mingw-w64-x86_64-toolchain
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-ninja
pacman -S mingw-w64-x86_64-ffmpeg

# Verify installations
gcc --version    # Should show GCC 11+ or 12+
cmake --version  # Should show CMake 3.20+
```

### 3. Install VS Code Extensions

1. Open VS Code
2. Go to Extensions (Ctrl+Shift+X)
3. Install these extensions:
   - **CMake Tools** (by Microsoft)
   - **C/C++** (by Microsoft)
   - **C/C++ Extension Pack** (optional, includes additional tools)

## VS Code Configuration

### 1. Add MinGW to System PATH (Optional but Recommended)

Add this to your system PATH environment variable:
```
C:\msys64\mingw64\bin
```

**Steps:**
1. Press Win+R, type `sysdm.cpl`, press Enter
2. Click "Advanced" tab → "Environment Variables"
3. Under "System variables", find and select "Path"
4. Click "Edit" → "New"
5. Add: `C:\msys64\mingw64\bin`
6. Click OK to save

### 2. Configure CMake Kit

1. Open the `screen-recorder` folder in VS Code
2. Press `Ctrl+Shift+P` → Type "CMake: Select Kit"
3. Choose **"GCC x.x.x x86_64-w64-mingw32"** (or similar MinGW option)
   - If you don't see it, click "Scan for kits" first

### 3. Select Configure Preset

1. Press `Ctrl+Shift+P` → Type "CMake: Select Configure Preset"
2. Choose **"mingw-release"**

The status bar at the bottom should now show:
- Kit: GCC ... MinGW
- Variant: Release

## Building the Project

### Method A: Using VS Code UI

1. **Configure CMake** (if not auto-configured):
   - Press `Ctrl+Shift+P` → "CMake: Configure"
   - Wait for configuration to complete

2. **Build**:
   - Press `Ctrl+Shift+P` → "CMake: Build"
   - Or click the build icon (hammer) in the status bar

3. **Check Output**:
   - Terminal shows build progress
   - On success: `[build] Build finished with exit code 0`

4. **Find Executable**:
   - Location: `build/mingw-release/bin/ScreenRecorder.exe`

### Method B: Using Command Line

Open a terminal in VS Code (Ctrl+`) and run:

```bash
cd build/mingw-release
cmake --build . --config Release
```

Or from project root:

```bash
mkdir -p build/mingw-release
cd build/mingw-release
cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -static" \
    ../..
cmake --build .
```

## Verifying the Build

### 1. Check File Size

```bash
ls -lh bin/ScreenRecorder.exe
```

Expected: 8-15 MB (depending on FFmpeg components included)

### 2. Check Dependencies

Use `ldd` or Dependency Walker to verify static linking:

```bash
# In MSYS2 terminal
ldd build/mingw-release/bin/ScreenRecorder.exe
```

Should show minimal dependencies (mainly Windows system DLLs).

### 3. Test Execution

Double-click `ScreenRecorder.exe`:
- No error dialogs should appear
- System tray icon should appear
- Press F9 → notification should appear
- Press F10 → stop notification should appear
- Check `%USERPROFILE%\Videos\Recordings\` for output file

## Troubleshooting

### Error: "CMake was unable to find a build program"

**Solution:** Install Ninja or use Make generator:
```bash
pacman -S mingw-w64-x86_64-ninja
```

Or reconfigure with Make:
```bash
cmake -G "MinGW Makefiles" ...
```

### Error: "FFmpeg headers not found"

**Solution:** Ensure FFmpeg dev package is installed:
```bash
pacman -S mingw-w64-x86_64-ffmpeg
```

Then set the include path explicitly:
```bash
cmake -DFFMPEG_DIR=C:/msys64/mingw64 ..
```

### Error: "undefined reference to avcodec_..."

**Cause:** Library linking order issue or missing static libs

**Solution:** 
1. Verify static libraries exist:
   ```bash
   ls /c/msys64/mingw64/lib/libavcodec*.a
   ls /c/msys64/mingw64/lib/libavformat*.a
   ls /c/msys64/mingw64/lib/libavutil*.a
   ```

2. Clean and rebuild:
   ```bash
   rm -rf build
   cmake ... 
   cmake --build .
   ```

### Error: "The code execution cannot proceed because libwinpthread-1.dll was not found"

**Cause:** Not fully statically linked

**Solution:** Add `-static` to linker flags:
```bash
cmake -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -static" ..
```

### IntelliSense Errors in VS Code

If you see red squiggles but build succeeds:

1. Press `Ctrl+Shift+P` → "C/C++: Select Configuration"
2. Choose the MinGW configuration
3. Or create `.vscode/c_cpp_properties.json`:

```json
{
    "configurations": [
        {
            "name": "MinGW",
            "compilerPath": "C:/msys64/mingw64/bin/g++.exe",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-gcc-x64",
            "includePath": [
                "${workspaceFolder}/**",
                "C:/msys64/mingw64/include/**"
            ],
            "defines": [
                "WIN32_LEAN_AND_MEAN",
                "NOMINMAX",
                "_DEBUG"
            ]
        }
    ],
    "version": 4
}
```

## Optimizing Build Size

To reduce executable size further:

### 1. Strip Debug Symbols

Add to CMakeLists.txt:
```cmake
add_custom_command(TARGET ScreenRecorder POST_BUILD
    COMMAND ${CMAKE_STRIP} $<TARGET_FILE:ScreenRecorder>
)
```

### 2. Enable Size Optimization

Change optimization flags:
```cmake
set(CMAKE_CXX_FLAGS_RELEASE "-Os -flto")
```

### 3. Exclude Unused FFmpeg Components

If you built FFmpeg yourself, ensure only needed components:
- avcodec (H.264 encoder)
- avformat (MP4 muxer)
- avutil (utilities)
- swscale (color conversion)

## Testing Output Compatibility

### Shotcut Verification

1. Open Shotcut
2. Drag recording file to timeline
3. Should import without "Transcoding required" warning
4. Playback should be smooth

### ffprobe Verification

```bash
ffprobe Recording_*.mp4
```

Expected output:
```
Stream #0:0: Video: h264 (Main), yuv420p, 1920x1080, 30 fps
```

### MediaInfo Verification

Download MediaInfo and check:
- Format: MPEG-4
- Codec: AVC/H.264
- Profile: Main@L4
- Scan type: Progressive
- Frame rate: 30.000 FPS constant

## Performance Testing

### CPU Usage Test

1. Open Task Manager → Details tab
2. Start recording (F9)
3. Observe ScreenRecorder.exe CPU usage
4. Expected: <15% on i5-5300U with QSV

### Memory Usage Test

1. Open Task Manager → Performance → Memory
2. Monitor during recording
3. Expected: <60MB total

### Frame Drop Test

Record for 60 seconds, then check:
```bash
ffprobe -show_frames -select_streams v Recording_*.mp4 2>&1 | grep -c "pkt_pts_time"
```

Expected: ~1800 frames (30 fps × 60 seconds)

## Next Steps

After successful build:

1. **Test on target hardware** (i5-5300U laptop)
2. **Verify video editor compatibility**
3. **Consider code signing** for distribution (prevents SmartScreen warnings)
4. **Create installer** (optional, for easy deployment)

## Quick Reference Commands

```bash
# Full clean rebuild
rm -rf build && mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..
cmake --build .

# Check executable
file bin/ScreenRecorder.exe
ls -lh bin/ScreenRecorder.exe

# Run tests
./bin/ScreenRecorder.exe
```

---

For additional help, refer to:
- CMake Documentation: https://cmake.org/documentation/
- MSYS2 Documentation: https://www.msys2.org/docs/
- FFmpeg Libavcodec Documentation: https://ffmpeg.org/doxygen/trunk/index.html
