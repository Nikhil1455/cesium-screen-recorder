// encoder.h - H.264 encoder (QSV/libx264)
#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
}

#include <cstdint>
#include <string>

// Encoder configuration
struct EncoderConfig {
    int width;
    int height;
    int fps;
    int bitrate;          // in kbps
    bool useQSV;          // Try Intel Quick Sync first
    std::string profile;  // "main", "baseline", "high"
};

class H264Encoder {
public:
    H264Encoder();
    ~H264Encoder();

    // Initialize encoder with config
    bool Initialize(const EncoderConfig& config);
    
    // Encode a frame (BGRA input -> H.264 packet)
    // Returns encoded packet size, 0 if no packet ready, -1 on error
    int EncodeFrame(const uint8_t* bgraData, int linesize, 
                    AVPacket* packet, bool* isKeyframe);
    
    // Flush encoder (call at end of recording)
    int FlushEncoder(AVPacket* packet);
    
    // Get codec parameters for muxer
    const AVCodecParameters* GetCodecParams() const;
    AVCodecContext* GetCodecContext() { return codecCtx_; }
    
    // Check if QSV is available
    static bool IsQSVAvailable();

private:
    bool InitializeQSV(const EncoderConfig& config);
    bool InitializeX264(const EncoderConfig& config);
    void Cleanup();

    AVCodecContext* codecCtx_;
    AVFrame* swsFrame_;      // For colorspace conversion
    struct SwsContext* swsCtx_;
    AVBufferRef* hwDeviceCtx_; // For QSV
    bool initialized_;
    bool usingQSV_;
    int frameCount_;
};
