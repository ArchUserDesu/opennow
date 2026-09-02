#include "opennow/yuv_convert.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace opennow {
namespace {

int clamp8(int value) {
    return std::max(0, std::min(255, value));
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

    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const int yy = frame.plane[0][static_cast<std::size_t>(y) * frame.stride[0] + x];
            int uu = 0;
            int vv = 0;

            if (frame.format == PixelFormat_YUV420P) {
                uu = frame.plane[1][static_cast<std::size_t>(y / 2) * frame.stride[1] + x / 2];
                vv = frame.plane[2][static_cast<std::size_t>(y / 2) * frame.stride[2] + x / 2];
            } else {
                const std::size_t uv_index =
                    static_cast<std::size_t>(y / 2) * frame.stride[1] +
                    static_cast<std::size_t>(x / 2) * 2;
                uu = frame.plane[1][uv_index + 0];
                vv = frame.plane[1][uv_index + 1];
            }

            int c = yy - 16;
            const int d = uu - 128;
            const int e = vv - 128;
            if (c < 0) {
                c = 0;
            }

            const int r = (298 * c + 409 * e + 128) >> 8;
            const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            const int b = (298 * c + 516 * d + 128) >> 8;

            out[static_cast<std::size_t>(y) * frame.width + x] =
                0xff000000u |
                (static_cast<std::uint32_t>(clamp8(r)) << 16) |
                (static_cast<std::uint32_t>(clamp8(g)) << 8) |
                static_cast<std::uint32_t>(clamp8(b));
        }
    }
    return true;
}

} // namespace opennow
