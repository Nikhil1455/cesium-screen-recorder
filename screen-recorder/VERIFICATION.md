# Verification Guide - MP4 Output Compatibility

This guide helps verify that recorded videos work correctly in Shotcut, OpenShot, and Kdenlive.

## Quick Verification Checklist

- [ ] Executable builds successfully (<15MB)
- [ ] Executable runs without errors
- [ ] System tray icon appears
- [ ] F9 starts recording with notification
- [ ] F10 stops recording with notification
- [ ] MP4 file created in Videos\Recordings
- [ ] File plays in VLC
- [ ] File imports into Shotcut without transcoding
- [ ] File imports into OpenShot without conversion
- [ ] File imports into Kdenlive smoothly

## Step 1: Verify Build Output

### Check Executable Size

```bash
cd build/mingw-release/bin
ls -lh ScreenRecorder.exe
```

**Expected:** 8-15 MB

If larger than 15MB:
- Debug symbols may be included (add `-s` to linker flags)
- Too many FFmpeg components linked

### Check Dependencies

```bash
# In MSYS2 terminal
ldd ScreenRecorder.exe | grep -v "System32\|SysWOW64\|ntdll"
```

**Expected:** Only Windows system DLLs, no MinGW runtime DLLs

If you see `libwinpthread-1.dll`, `libgcc_s_seh-1.dll`, or `libstdc++-6.dll`:
- Static linking not configured correctly
- Rebuild with `-static-libgcc -static-libstdc++ -static`

## Step 2: Test Recording

### Create Test Recording

1. Run `ScreenRecorder.exe`
2. Wait for tray icon
3. Press F9 (start)
4. Wait for "Recording Started" notification
5. Move mouse, open some windows (test content)
6. Wait 10 seconds
7. Press F10 (stop)
8. Wait for "Recording Stopped" notification

### Check Output File

```bash
cd "$USERPROFILE/Videos/Recordings"
dir *.mp4
```

**Expected:**
- File exists with timestamp name
- File size >0 (typically 5-10MB for 10 seconds)

## Step 3: Technical Verification with ffprobe

Install FFmpeg tools if not already installed:
```bash
pacman -S mingw-w64-x86_64-ffmpeg
```

### Basic Stream Info

```bash
ffprobe Recording_*.mp4 2>&1 | findstr "Stream"
```

**Expected Output:**
```
Stream #0:0(und): Video: h264 (Main) (avc1 / 0x31637661), yuv420p, 1920x1080 [...], 30 fps, 30 tbr
```

**Check:**
- ✅ Codec: h264 (not mjpeg, not hevc)
- ✅ Profile: Main (not Baseline, not High)
- ✅ Pixel format: yuv420p (not bgra, not rgb)
- ✅ Resolution: 1920x1080 (or your screen resolution)
- ✅ Frame rate: 30 fps (constant)

### Detailed Video Analysis

```bash
ffprobe -v error -show_entries stream=codec_name,codec_type,profile,width,height,r_frame_rate,pix_fmt -of json Recording_*.mp4
```

**Expected JSON:**
```json
{
    "streams": [
        {
            "codec_name": "h264",
            "codec_type": "video",
            "profile": "Main",
            "width": 1920,
            "height": 1080,
            "r_frame_rate": "30/1",
            "pix_fmt": "yuv420p"
        }
    ]
}
```

### Check for Frame Drops

```bash
ffprobe -v error -select_streams v -show_frames Recording_*.mp4 2>&1 | findstr /C:"pkt_pts_time" | find /C /V ""
```

**Expected:** ~300 frames for 10 seconds (30 fps × 10s)

If significantly fewer frames:
- Capture thread may be dropping frames
- Encoder too slow, falling behind
- System resource constraints

### Verify Constant Frame Rate

```bash
ffprobe -v error -select_streams v -show_entries frame=pkt_pts_time -of csv=p=0 Recording_*.mp4 > frames.txt
```

Then analyze frame intervals (should all be ~0.033 seconds apart).

## Step 4: VLC Playback Test

1. Open VLC Media Player
2. Drag recording file to VLC window
3. Watch entire video

**Check:**
- ✅ Video plays smoothly (no stuttering)
- ✅ No visual artifacts or corruption
- ✅ Audio sync not applicable (no audio)
- ✅ Duration matches expected recording time

**VLC Codec Information:**
- Tools → Codec Information
- Should show: H.264 - MPEG-4 AVC (part 10)

## Step 5: Shotcut Verification

### Import Test

1. Open Shotcut (v22.0 or later)
2. Click "Open File" or drag MP4 to Project panel
3. **CRITICAL:** Should NOT show "Transcoding required" dialog

If transcoding required:
- ❌ Wrong codec profile
- ❌ Wrong pixel format
- ❌ Variable frame rate instead of constant

### Timeline Test

1. Drag clip from Project panel to Timeline
2. Scrub through timeline (drag playhead)
3. Press Play

**Check:**
- ✅ Preview renders in real-time
- ✅ No dropped frames during playback
- ✅ Export settings recognize H.264

### Export Test

1. Select clip on timeline
2. Export → H.264 Main Profile
3. Export file should play correctly

## Step 6: OpenShot Verification

### Import Test

1. Open OpenShot (v3.0 or later)
2. Click "Import Files" or drag MP4 to Project files
3. **CRITICAL:** Should import immediately without conversion dialog

### Properties Check

1. Right-click imported file → Properties
2. Check displayed information:

**Expected:**
- Video Codec: H.264
- Frame Rate: 30 fps
- Resolution: 1920x1080

### Timeline Test

1. Drag clip to Track
2. Play preview
3. Test seek forward/backward

**Check:**
- ✅ Smooth playback
- ✅ Accurate seeking
- ✅ No green frames or corruption

## Step 7: Kdenlive Verification

### Import Test

1. Open Kdenlive (v22.0 or later)
2. Project Bin → Add Clip or drag MP4
3. Should add without "Clip needs conversion" warning

If conversion warned:
- Click "Analyze" to see why
- Common issues: variable framerate, wrong pixel format

### Clip Properties

1. Right-click clip → Properties
2. Check Metadata tab

**Expected:**
- Format: yuv420p
- Frame rate: 30
- Field order: Progressive

### Sequence Test

1. Drag clip to Timeline
2. Create sequence matching clip properties
3. Render test

**Render Settings:**
- Format: MP4
- Video codec: H.264
- Should complete quickly (minimal re-encoding)

## Step 8: Cross-Platform Testing (Optional)

Test on Linux to ensure compatibility:

```bash
# On Linux with ffmpeg
ffprobe Recording_*.mp4

# On Linux with VLC
vlc Recording_*.mp4

# On Linux with Shotcut
shotcut Recording_*.mp4
```

## Common Issues and Fixes

### Issue: "Transcoding required" in Shotcut

**Cause:** Non-standard profile or pixel format

**Fix:** Verify encoder settings:
```cpp
// In encoder.cpp, ensure:
av_opt_set(codecCtx_->priv_data, "profile", "main", 0);
codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P; // or NV12 for QSV
```

### Issue: Green/purple frames in editor

**Cause:** Incorrect pixel format or byte order

**Fix:** Check colorspace conversion:
```cpp
// Ensure proper BGRA to YUV conversion
swsCtx_ = sws_getContext(
    width, height, AV_PIX_FMT_BGRA,
    width, height, AV_PIX_FMT_YUV420P,
    SWS_FAST_BILINEAR, ...
);
```

### Issue: A/V sync drift (if audio added later)

**Cause:** Variable frame rate or incorrect timestamps

**Fix:** Ensure CFR and proper PTS:
```cpp
// Use consistent timebase
codecCtx_->time_base = AVRational{1, 30};
codecCtx_->framerate = AVRational{30, 1};
packet->pts = av_rescale_q(frameIndex, AVRational{1, 30}, stream->time_base);
```

### Issue: Choppy playback

**Cause:** Dropped frames during capture

**Fix:** 
- Increase queue size
- Lower capture resolution
- Ensure QSV is being used (check CPU usage)

### Issue: Large file size

**Cause:** Bitrate too high or inefficient encoding

**Fix:** Adjust bitrate in main.cpp:
```cpp
constexpr int TARGET_BITRATE = 5000; // Reduce to 3000-4000
```

## Performance Benchmarks

### Expected Results (i5-5300U)

| Metric | Minimum | Target | Excellent |
|--------|---------|--------|-----------|
| CPU Usage | <20% | <15% | <10% |
| RAM Usage | <100MB | <60MB | <40MB |
| Frame Drop | <5% | <1% | 0% |
| File Size/hr | <3GB | ~2.2GB | ~1.8GB |

### How to Measure

**CPU Usage:**
1. Task Manager → Details tab
2. Right-click column headers → Select columns
3. Enable "CPU" and "CPU time"
4. Record for 60 seconds, note average

**RAM Usage:**
1. Task Manager → Details tab
2. Find ScreenRecorder.exe
3. Note "Memory (private working set)"

**Frame Drop:**
```bash
# Count expected vs actual frames
ffprobe -v error -select_streams v -show_frames input.mp4 | \
  grep -c "media_type=video"
# Should be duration_seconds × 30
```

## Final Checklist for Distribution

Before sharing the .exe:

- [ ] Tested on clean Windows 10/11 VM (no dev tools)
- [ ] No external DLLs required
- [ ] SmartScreen warning acceptable (or code-signed)
- [ ] Works on both Intel and AMD CPUs
- [ ] Works on integrated and discrete GPUs
- [ ] Verified with multiple video editors
- [ ] README includes troubleshooting steps
- [ ] License information included

## Automated Test Script

Create `verify_output.bat`:

```batch
@echo off
echo === Screen Recorder Verification ===
echo.

REM Find latest recording
for %%f in ("%USERPROFILE%\Videos\Recordings\*.mp4") do set LATEST=%%f

if "%LATEST%"=="" (
    echo ERROR: No recordings found!
    exit /b 1
)

echo Latest recording: %LATEST%
echo.

echo === ffprobe Analysis ===
ffprobe -v error -show_entries stream=codec_name,profile,width,height,r_frame_rate,pix_fmt -of compact "%LATEST%"

echo.
echo === File Info ===
for %%A in ("%LATEST%") do echo Size: %%~zA bytes

echo.
echo === Quick Test ===
echo Opening in VLC...
start vlc "%LATEST%"

echo.
echo Verification complete!
```

Run after making a test recording.

---

For questions or issues, refer to the main README.md or BUILD_GUIDE.md.
