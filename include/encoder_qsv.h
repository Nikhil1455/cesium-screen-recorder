#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_qsv.h>
}

#include <string>
#include <atomic>
#include <memory>

#include "ring_buffer.h"

// Encoder configuration
struct EncoderConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate_kbps = 4000;  // 4 Mbps default
    std::string encoder_type = "qsv";  // "qsv" or "x264"
    std::string profile = "main";
    int gop_size = 30;  // Keyframe interval
};

// Intel Quick Sync Video encoder via FFmpeg
// Prioritized for Intel HD 5500 (Broadwell Gen8)
class QSVEncoder {
public:
    QSVEncoder();
    ~QSVEncoder();

    // Initialize QSV encoder
    // Returns true on success, false if QSV unavailable (fallback needed)
    bool initialize(const EncoderConfig& config);
    
    // Encode a single frame (called from encode thread)
    // Returns encoded packet data or nullptr if no packet ready
    AVPacket* encode_frame(AVFrame* input_frame);
    
    // Flush encoder (call before closing)
    AVPacket* flush();
    
    // Check if QSV is available on this system
    static bool is_available();
    
    // Get the AVCodecContext for muxer integration
    AVCodecContext* get_codec_context() { return codec_ctx_; }
    
    // Cleanup resources
    void release();

private:
    class QSVResources {
    public:
        AVBufferRef* hw_device_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        const AVCodec* codec = nullptr;
        
        void release_all() {
            if (codec_ctx) {
                avcodec_free_context(&codec_ctx);
            }
            if (hw_device_ctx) {
                av_buffer_unref(&hw_device_ctx);
            }
            codec = nullptr;
        }
    };

    bool create_hw_device_context();
    bool configure_encoder(const EncoderConfig& config);
    void transfer_to_hw_frame(AVFrame* hw_frame, AVFrame* sw_frame);

    QSVResources resources_;
    AVFrame* hw_frame_ = nullptr;
    AVFrame* sw_frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    EncoderConfig config_;
    std::atomic<bool> initialized_{false};
    bool use_qsv_ = true;
};

// x264 software encoder fallback for dual-core systems
// Uses ultrafast preset with zerolatency tune
class X264Encoder {
public:
    X264Encoder();
    ~X264Encoder();

    // Initialize x264 encoder
    bool initialize(const EncoderConfig& config);
    
    // Encode a single frame
    AVPacket* encode_frame(AVFrame* input_frame);
    
    // Flush encoder
    AVPacket* flush();
    
    // Get the AVCodecContext
    AVCodecContext* get_codec_context() { return codec_ctx_; }
    
    // Cleanup
    void release();

private:
    AVCodecContext* codec_ctx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    AVPacket* packet_ = nullptr;
    EncoderConfig config_;
    std::atomic<bool> initialized_{false};
};

// Factory function to create appropriate encoder
std::unique_ptr<QSVEncoder> create_encoder(const EncoderConfig& config, bool& used_qsv);
