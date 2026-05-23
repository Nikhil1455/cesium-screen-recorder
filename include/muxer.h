#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
}

#include <string>
#include <atomic>
#include <memory>

#include "encoder_qsv.h"

// MP4 muxer for H.264 output
// Writes CFR (Constant Frame Rate) MP4 files
class MP4Muxer {
public:
    MP4Muxer();
    ~MP4Muxer();

    // Initialize muxer with output file path
    bool initialize(const std::string& output_path, AVCodecContext* codec_ctx);
    
    // Write an encoded packet to the output file
    bool write_packet(AVPacket* packet);
    
    // Finalize and close the output file
    bool finalize();
    
    // Cleanup resources
    void release();

private:
    class MuxerResources {
    public:
        AVFormatContext* format_ctx = nullptr;
        int video_stream_index = -1;
        
        void release_all() {
            if (format_ctx) {
                // Write trailer and close
                if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
                    av_write_trailer(format_ctx);
                    avio_closep(&format_ctx->pb);
                }
                avformat_free_context(format_ctx);
                format_ctx = nullptr;
            }
            video_stream_index = -1;
        }
    };

    MuxerResources resources_;
    std::string output_path_;
    std::atomic<bool> initialized_{false};
    int64_t start_time_ = 0;
    AVRational time_base_ = {1, 30};  // Default, updated during init
};
