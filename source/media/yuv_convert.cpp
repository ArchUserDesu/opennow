#include "opennow/yuv_convert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace opennow {
namespace {

int clamp8(int value) {
    return std::max(0, std::min(255, value));
}

inline std::uint32_t convert_yuv_pixel(int yy, int red_chroma, int green_chroma, int blue_chroma) {
    int c = yy - 16;
    if (c < 0) c = 0;
    const int luminance = 298 * c;
    return 0xff000000u |
        (static_cast<std::uint32_t>(clamp8((luminance + red_chroma) >> 8)) << 16) |
        (static_cast<std::uint32_t>(clamp8((luminance + green_chroma) >> 8)) << 8) |
        static_cast<std::uint32_t>(clamp8((luminance + blue_chroma) >> 8));
}

bool has_plane_bytes(const std::vector<std::uint8_t>& plane,
                     int stride,
                     int rows,
                     int minimum_row_bytes) {
    if (stride < minimum_row_bytes || rows <= 0) {
        return false;
    }
    return plane.size() >= static_cast<std::size_t>(stride) * static_cast<std::size_t>(rows);
}

} // namespace

bool to_argb8888(const VideoFrame& frame, std::vector<std::uint32_t>& out) {
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }

    const std::size_t pixel_count =
        static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    out.resize(pixel_count);

    if (frame.format == PixelFormat_ARGB8888) {
        if (!has_plane_bytes(frame.plane[0], frame.stride[0], frame.height, frame.width * 4)) {
            return false;
        }
        for (int y = 0; y < frame.height; ++y) {
            const std::uint8_t* src =
                &frame.plane[0][0] + static_cast<std::size_t>(y) * frame.stride[0];
            for (int x = 0; x < frame.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(x) * 4;
                out[static_cast<std::size_t>(y) * frame.width + x] =
                    (static_cast<std::uint32_t>(src[i + 0]) << 24) |
                    (static_cast<std::uint32_t>(src[i + 1]) << 16) |
                    (static_cast<std::uint32_t>(src[i + 2]) << 8) |
                    static_cast<std::uint32_t>(src[i + 3]);
            }
        }
        return true;
    }

    if (frame.format == PixelFormat_YUV420P) {
        const int chroma_width = (frame.width + 1) / 2;
        const int chroma_height = (frame.height + 1) / 2;
        if (!has_plane_bytes(frame.plane[0], frame.stride[0], frame.height, frame.width) ||
            !has_plane_bytes(frame.plane[1], frame.stride[1], chroma_height, chroma_width) ||
            !has_plane_bytes(frame.plane[2], frame.stride[2], chroma_height, chroma_width)) {
            return false;
        }
    } else if (frame.format == PixelFormat_NV12) {
        const int chroma_height = (frame.height + 1) / 2;
        const int chroma_row_bytes = ((frame.width + 1) / 2) * 2;
        if (!has_plane_bytes(frame.plane[0], frame.stride[0], frame.height, frame.width) ||
            !has_plane_bytes(frame.plane[1], frame.stride[1], chroma_height, chroma_row_bytes)) {
            return false;
        }
    } else {
        return false;
    }

    /* Traverse by 2x2 chroma blocks.  Four output pixels share U/V, cutting
       chroma addressing and coefficient work by roughly 75 percent. */
    const int chroma_width = (frame.width + 1) / 2;
    const int chroma_height = (frame.height + 1) / 2;
    for (int cy = 0; cy < chroma_height; ++cy) {
        for (int cx = 0; cx < chroma_width; ++cx) {
            int uu, vv;
            if (frame.format == PixelFormat_YUV420P) {
                uu = frame.plane[1][static_cast<std::size_t>(cy) * frame.stride[1] + cx];
                vv = frame.plane[2][static_cast<std::size_t>(cy) * frame.stride[2] + cx];
            } else {
                const std::size_t uv = static_cast<std::size_t>(cy) * frame.stride[1] + static_cast<std::size_t>(cx) * 2;
                uu = frame.plane[1][uv]; vv = frame.plane[1][uv + 1];
            }
            const int d = uu - 128, e = vv - 128;
            const int red_chroma = 409 * e + 128;
            const int green_chroma = -100 * d - 208 * e + 128;
            const int blue_chroma = 516 * d + 128;
            for (int dy = 0; dy < 2; ++dy) {
                const int y = cy * 2 + dy; if (y >= frame.height) break;
                const std::size_t yrow = static_cast<std::size_t>(y) * frame.stride[0];
                const std::size_t outrow = static_cast<std::size_t>(y) * frame.width;
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = cx * 2 + dx; if (x >= frame.width) break;
                    out[outrow + x] = convert_yuv_pixel(frame.plane[0][yrow + x], red_chroma, green_chroma, blue_chroma);
                }
            }
        }
    }
    return true;
}

} // namespace opennow
