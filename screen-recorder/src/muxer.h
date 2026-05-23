// muxer.h - MP4 muxer for H.264 output
#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <string>

class MP4Muxer {
public:
    MP4Muxer();
    ~MP4Muxer();

    // Initialize muxer with output filename and encoder context
    bool Initialize(const std::wstring& filename, AVCodecContext* codecCtx);
    
    // Write an encoded packet to the file
    bool WritePacket(AVPacket* packet, int64_t pts);
    
    // Finalize and close the file
    bool Finalize();
    
    // Check if muxer is ready
    bool IsInitialized() const { return initialized_; }

private:
    AVFormatContext* formatCtx_;
    AVStream* videoStream_;
    int videoStreamIdx_;
    bool initialized_;
};
