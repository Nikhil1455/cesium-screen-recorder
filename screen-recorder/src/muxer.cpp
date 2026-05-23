// muxer.cpp - MP4 muxer implementation
#include "muxer.h"
#include "utils.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

MP4Muxer::MP4Muxer()
    : formatCtx_(nullptr), videoStream_(nullptr), videoStreamIdx_(-1), 
      initialized_(false) {}

MP4Muxer::~MP4Muxer() {
    if (initialized_) {
        Finalize();
    }
}

bool MP4Muxer::Initialize(const std::wstring& filename, AVCodecContext* codecCtx) {
    if (!codecCtx) {
        LogError("Invalid codec context");
        return false;
    }
    
    // Create output format context
    int ret = avformat_alloc_output_context2(&formatCtx_, nullptr, "mp4", nullptr);
    if (ret < 0 || !formatCtx_) {
        LogError("Failed to create output format context");
        return false;
    }
    
    // Create video stream
    videoStream_ = avformat_new_stream(formatCtx_, nullptr);
    if (!videoStream_) {
        LogError("Failed to create video stream");
        return false;
    }
    
    videoStreamIdx_ = videoStream_->index;
    
    // Copy codec parameters from encoder
    ret = avcodec_parameters_from_context(videoStream_->codecpar, codecCtx);
    if (ret < 0) {
        LogError("Failed to copy codec parameters");
        return false;
    }
    
    // Set stream timebase
    videoStream_->time_base = codecCtx->time_base;
    
    // Set MP4-specific options for better compatibility
    av_opt_set(formatCtx_->priv_data, "movflags", "+faststart", 0);
    av_opt_set(formatCtx_->priv_data, "brand", "isom", 0);
    
    // Open output file
    // Convert wide string to UTF-8 for FFmpeg
    int wideLen = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, 
                                       nullptr, 0, nullptr, nullptr);
    std::string utf8Filename(wideLen, 0);
    WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, 
                        &utf8Filename[0], wideLen, nullptr, nullptr);
    
    // Remove null terminator from count
    if (!utf8Filename.empty() && utf8Filename.back() == '\0') {
        utf8Filename.pop_back();
    }
    
    ret = avio_open(&formatCtx_->pb, utf8Filename.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LogError(errBuf);
        return false;
    }
    
    // Write header
    ret = avformat_write_header(formatCtx_, nullptr);
    if (ret < 0) {
        LogError("Failed to write header");
        avio_close(formatCtx_->pb);
        return false;
    }
    
    initialized_ = true;
    LOG_DEBUG("MP4 muxer initialized: %s", utf8Filename.c_str());
    return true;
}

bool MP4Muxer::WritePacket(AVPacket* packet, int64_t pts) {
    if (!initialized_ || !packet) {
        return false;
    }
    
    // Rescale PTS/DTS to stream timebase
    AVRational tb = videoStream_->time_base;
    packet->pts = av_rescale_q(pts, AVRational{1, 30}, tb);
    packet->dts = packet->pts;
    packet->duration = 1;
    packet->stream_index = videoStreamIdx_;
    
    int ret = av_interleaved_write_frame(formatCtx_, packet);
    if (ret < 0) {
        char errBuf[256];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LogError(errBuf);
        return false;
    }
    
    return true;
}

bool MP4Muxer::Finalize() {
    if (!initialized_) {
        return false;
    }
    
    // Write trailer
    av_write_trailer(formatCtx_);
    
    // Close IO
    avio_close(formatCtx_->pb);
    
    // Free context
    avformat_free_context(formatCtx_);
    
    formatCtx_ = nullptr;
    videoStream_ = nullptr;
    videoStreamIdx_ = -1;
    initialized_ = false;
    
    LOG_DEBUG("MP4 muxer finalized");
    return true;
}
