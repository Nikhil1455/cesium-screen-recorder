#include "muxer.h"
#include "utils.h"

MP4Muxer::MP4Muxer() = default;

MP4Muxer::~MP4Muxer() {
    release();
}

bool MP4Muxer::initialize(const std::string& output_path, AVCodecContext* codec_ctx) {
    output_path_ = output_path;
    
    // Allocate format context for MP4
    int ret = avformat_alloc_output_context2(&resources_.format_ctx, nullptr, "mp4", output_path.c_str());
    if (!check_av_error(ret, "avformat_alloc_output_context2")) {
        return false;
    }
    
    // Create video stream
    AVStream* stream = avformat_new_stream(resources_.format_ctx, nullptr);
    if (!stream) {
        LOG_ERROR("Failed to create video stream");
        return false;
    }
    
    resources_.video_stream_index = stream->index;
    
    // Copy codec parameters from encoder to stream
    ret = avcodec_parameters_from_context(stream->codecpar, codec_ctx);
    if (!check_av_error(ret, "avcodec_parameters_from_context")) {
        return false;
    }
    
    // Set time base for CFR output
    stream->time_base = codec_ctx->time_base;
    time_base_ = stream->time_base;
    
    // Set codec tag for H.264 in MP4
    stream->codecpar->codec_tag = 0;  // Let FFmpeg determine the correct tag
    
    // Open output file
    if (!(resources_.format_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&resources_.format_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE);
        if (!check_av_error(ret, "avio_open")) {
            return false;
        }
    }
    
    // Write header
    AVDictionary* opts = nullptr;
    // Optimize for streaming: moov atom at start
    av_dict_set(&opts, "movflags", "+faststart", 0);
    // Use baseline profile compatibility
    av_dict_set(&opts, "brand", "isom", 0);
    
    ret = avformat_write_header(resources_.format_ctx, &opts);
    if (!check_av_error(ret, "avformat_write_header")) {
        av_dict_free(&opts);
        return false;
    }
    
    av_dict_free(&opts);
    
    // Record start time for PTS calculation
    start_time_ = av_gettime_relative();
    
    initialized_.store(true, std::memory_order_release);
    LOG_INFO("MP4 muxer initialized: %s (stream %d)", output_path.c_str(), resources_.video_stream_index);
    return true;
}

bool MP4Muxer::write_packet(AVPacket* packet) {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    
    if (!packet || packet->size <= 0) {
        return false;
    }
    
    // Ensure packet is on the video stream
    if (packet->stream_index == -1) {
        packet->stream_index = resources_.video_stream_index;
    }
    
    // Rescale PTS/DTS to stream time base if needed
    AVRational codec_tb = {1, 90000};  // Encoder time base
    if (packet->pts != AV_NOPTS_VALUE) {
        packet->pts = av_rescale_q(packet->pts, codec_tb, resources_.format_ctx->streams[resources_.video_stream_index]->time_base);
    }
    if (packet->dts != AV_NOPTS_VALUE) {
        packet->dts = av_rescale_q(packet->dts, codec_tb, resources_.format_ctx->streams[resources_.video_stream_index]->time_base);
    }
    
    // Write packet to output file
    int ret = av_interleaved_write_frame(resources_.format_ctx, packet);
    if (!check_av_error(ret, "av_interleaved_write_frame")) {
        return false;
    }
    
    return true;
}

bool MP4Muxer::finalize() {
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    
    // Write trailer (finalizes the file)
    int ret = av_write_trailer(resources_.format_ctx);
    if (!check_av_error(ret, "av_write_trailer")) {
        return false;
    }
    
    // Close output file
    if (!(resources_.format_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_closep(&resources_.format_ctx->pb);
        if (!check_av_error(ret, "avio_closep")) {
            return false;
        }
    }
    
    initialized_.store(false, std::memory_order_release);
    LOG_INFO("MP4 file finalized: %s", output_path_.c_str());
    return true;
}

void MP4Muxer::release() {
    resources_.release_all();
    LOG_INFO("MP4 muxer resources released");
}
