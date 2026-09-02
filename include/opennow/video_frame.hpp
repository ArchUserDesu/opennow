#pragma once
#include <cstdint>
#include <vector>
namespace opennow {
enum PixelFormat { PixelFormat_YUV420P, PixelFormat_NV12, PixelFormat_ARGB8888 };
struct VideoFrame {
    int width,height; PixelFormat format; int stride[3]; std::vector<std::uint8_t> plane[3]; std::int64_t pts;
    VideoFrame():width(0),height(0),format(PixelFormat_YUV420P),pts(0){stride[0]=stride[1]=stride[2]=0;}
};
}
