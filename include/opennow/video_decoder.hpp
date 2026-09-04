#pragma once
#include "video_frame.hpp"
#include <cstddef>
#include <cstdint>
namespace opennow {
enum VideoDecodeResult {
    VideoDecodeError = -1,
    VideoDecodeNoFrame = 0,
    VideoDecodeFrame = 1
};
class VideoDecoder {
public:
    virtual ~VideoDecoder(){}
    virtual bool open(int w,int h,int fps)=0;
    virtual VideoDecodeResult decode(const std::uint8_t* data,std::size_t size,VideoFrame& out)=0;
    virtual void flush()=0;
};
VideoDecoder* make_ffmpeg_h264_decoder();
}
