// encoder.cpp - H.264 encoder implementation (QSV/libx264)
#include "encoder.h"
#include "utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_qsv.h>
#include <libswscale/swscale.h>
}

H264Encoder::H264Encoder()
    : codecCtx_(nullptr), swsFrame_(nullptr), swsCtx_(nullptr),
      hwDeviceCtx_(nullptr), initialized_(false), usingQSV_(false), frameCount_(0) {}

H264Encoder::~H264Encoder() {
    Cleanup();
}

bool H264Encoder::IsQSVAvailable() {
    // Check if QSV codec is available in FFmpeg build
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) {
        return false;
    }
    
    // Try to create a hardware device context for QSV
    AVBufferRef* hwCtx = nullptr;
    int ret = av_hwdevice_ctx_create(&hwCtx, AV_HWDEVICE_TYPE_QSV, nullptr, nullptr, 0);
    if (ret >= 0) {
        av_buffer_unref(&hwCtx);
        return true;
    }
    
    return false;
}

bool H264Encoder::Initialize(const EncoderConfig& config) {
    if (initialized_) {
        return true;
    }
    
    // Try QSV first if requested and available
    if (config.useQSV && IsQSVAvailable()) {
        LOG_DEBUG("Attempting QSV initialization");
        if (InitializeQSV(config)) {
            usingQSV_ = true;
            initialized_ = true;
            LOG_DEBUG("QSV encoder initialized successfully");
            return true;
        }
        LOG_DEBUG("QSV initialization failed, falling back to libx264");
    } else if (config.useQSV) {
        LogError("QSV requested but not available");
    }
    
    // Fallback to libx264
    if (InitializeX264(config)) {
        usingQSV_ = false;
        initialized_ = true;
        LOG_DEBUG("libx264 encoder initialized successfully");
        return true;
    }
    
    LogError("Failed to initialize any H.264 encoder");
    return false;
}

bool H264Encoder::InitializeQSV(const EncoderConfig& config) {
    // Create QSV hardware device context
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_QSV, 
                                      nullptr, nullptr, 0);
    if (ret < 0) {
        LogError("Failed to create QSV hardware device context");
        return false;
    }
    
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) {
        return false;
    }
    
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        return false;
    }
    
    codecCtx_->width = config.width;
    codecCtx_->height = config.height;
    codecCtx_->time_base = AVRational{1, config.fps};
    codecCtx_->framerate = AVRational{config.fps, 1};
    codecCtx_->bit_rate = config.bitrate * 1000;
    codecCtx_->pix_fmt = AV_PIX_FMT_NV12;  // QSV native format
    codecCtx_->gop_size = config.fps;       // Keyframe every second
    
    // Transfer hardware context
    codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    if (!codecCtx_->hw_device_ctx) {
        Cleanup();
        return false;
    }
    
    // Set QSV-specific options
    av_opt_set(codecCtx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(codecCtx_->priv_data, "profile", config.profile.c_str(), 0);
    
    // Async depth for better performance
    av_opt_set_int(codecCtx_->priv_data, "async_depth", 4, 0);
    
    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LogError(errBuf);
        Cleanup();
        return false;
    }
    
    // Create conversion frame (BGRA -> NV12)
    swsFrame_ = av_frame_alloc();
    if (!swsFrame_) {
        Cleanup();
        return false;
    }
    
    swsFrame_->format = AV_PIX_FMT_NV12;
    swsFrame_->width = config.width;
    swsFrame_->height = config.height;
    
    ret = av_frame_get_buffer(swsFrame_, 32);
    if (ret < 0) {
        Cleanup();
        return false;
    }
    
    // Create colorspace conversion context
    swsCtx_ = sws_getContext(
        config.width, config.height, AV_PIX_FMT_BGRA,
        config.width, config.height, AV_PIX_FMT_NV12,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!swsCtx_) {
        Cleanup();
        return false;
    }
    
    return true;
}

bool H264Encoder::InitializeX264(const EncoderConfig& config) {
#ifdef _MSC_VER
#warning "Intel MFX/QSV libraries unavailable in MinGW toolchain, falling back to libx264"
#else
#warning "Intel MFX/QSV libraries unavailable, falling back to libx264"
#endif
    
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        // Fallback to generic h264 encoder
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    
    if (!codec) {
        LogError("No H.264 encoder found");
        return false;
    }
    
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        return false;
    }
    
    codecCtx_->width = config.width;
    codecCtx_->height = config.height;
    codecCtx_->time_base = AVRational{1, config.fps};
    codecCtx_->framerate = AVRational{config.fps, 1};
    codecCtx_->bit_rate = config.bitrate * 1000;
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx_->gop_size = config.fps;
    
    // x264-specific options for low CPU usage
    codecCtx_->compression_level = 1;  // ultrafast preset equivalent
    
    // Set profile
    if (config.profile == "main") {
        av_opt_set(codecCtx_->priv_data, "profile", "main", 0);
    } else if (config.profile == "baseline") {
        av_opt_set(codecCtx_->priv_data, "profile", "baseline", 0);
    } else {
        av_opt_set(codecCtx_->priv_data, "profile", "main", 0);
    }
    
    // Ultrafast preset for minimal CPU usage
    av_opt_set(codecCtx_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codecCtx_->priv_data, "tune", "zerolatency", 0);
    
    int ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LogError(errBuf);
        Cleanup();
        return false;
    }
    
    // Create conversion frame (BGRA -> YUV420P)
    swsFrame_ = av_frame_alloc();
    if (!swsFrame_) {
        Cleanup();
        return false;
    }
    
    swsFrame_->format = AV_PIX_FMT_YUV420P;
    swsFrame_->width = config.width;
    swsFrame_->height = config.height;
    
    ret = av_frame_get_buffer(swsFrame_, 32);
    if (ret < 0) {
        Cleanup();
        return false;
    }
    
    // Create colorspace conversion context
    swsCtx_ = sws_getContext(
        config.width, config.height, AV_PIX_FMT_BGRA,
        config.width, config.height, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!swsCtx_) {
        Cleanup();
        return false;
    }
    
    return true;
}

int H264Encoder::EncodeFrame(const uint8_t* bgraData, int linesize,
                              AVPacket* packet, bool* isKeyframe) {
    if (!initialized_ || !codecCtx_) {
        return -1;
    }
    
    av_new_packet(packet, codecCtx_->width * codecCtx_->height * 3);
    
    // Convert BGRA to encoder pixel format (NV12 or YUV420P)
    const uint8_t* srcSlice[4] = {bgraData, nullptr, nullptr, nullptr};
    int srcStride[4] = {linesize, 0, 0, 0};
    
    sws_scale(swsCtx_, srcSlice, srcStride, 0, codecCtx_->height,
              swsFrame_->data, swsFrame_->linesize);
    
    swsFrame_->pts = frameCount_;
    
    int ret = avcodec_send_frame(codecCtx_, swsFrame_);
    if (ret < 0) {
        return -1;
    }
    
    ret = avcodec_receive_packet(codecCtx_, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        av_packet_unref(packet);
        return 0;
    }
    
    if (ret < 0) {
        return -1;
    }
    
    *isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
    frameCount_++;
    
    return packet->size;
}

int H264Encoder::FlushEncoder(AVPacket* packet) {
    if (!codecCtx_) {
        return 0;
    }
    
    avcodec_send_frame(codecCtx_, nullptr);
    
    int ret = avcodec_receive_packet(codecCtx_, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
    }
    
    if (ret < 0) {
        return -1;
    }
    
    return packet->size;
}

const AVCodecParameters* H264Encoder::GetCodecParams() const {
    // Note: In production, you'd copy these from codecCtx_
    // For simplicity, we return nullptr and let muxer create from context
    return nullptr;
}

void H264Encoder::Cleanup() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    
    if (swsFrame_) {
        av_frame_free(&swsFrame_);
    }
    
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
    }
    
    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
    }
    
    initialized_ = false;
    usingQSV_ = false;
    frameCount_ = 0;
}
