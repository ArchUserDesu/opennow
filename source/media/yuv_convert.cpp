#include "opennow/yuv_convert.hpp"

#include <cstddef>
#include <cstdint>

namespace opennow {
namespace {

inline int clamp8_fast(int value) {
    if ((unsigned)value <= 255u) return value;
    return value < 0 ? 0 : 255;
}

inline std::uint32_t pack_yuv(int yy, int red_chroma, int green_chroma, int blue_chroma) {
    int c = yy - 16;
    if (c < 0) c = 0;
    const int lum = 298 * c;
    const int r = clamp8_fast((lum + red_chroma) >> 8);
    const int g = clamp8_fast((lum + green_chroma) >> 8);
    const int b = clamp8_fast((lum + blue_chroma) >> 8);
    return 0xff000000u |
        (static_cast<std::uint32_t>(r) << 16) |
        (static_cast<std::uint32_t>(g) << 8) |
        static_cast<std::uint32_t>(b);
}

bool has_plane_bytes(const std::vector<std::uint8_t>& plane,
                     int stride,
                     int rows,
                     int minimum_row_bytes) {
    if (stride < minimum_row_bytes || rows <= 0) return false;
    return plane.size() >= static_cast<std::size_t>(stride) * static_cast<std::size_t>(rows);
}

} // namespace

bool to_argb8888(const VideoFrame& frame, std::vector<std::uint32_t>& out) {
    if (frame.width <= 0 || frame.height <= 0) return false;

    const int width = frame.width;
    const int height = frame.height;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    out.resize(pixel_count);

    if (frame.format == PixelFormat_ARGB8888) {
        if (!has_plane_bytes(frame.plane[0], frame.stride[0], height, width * 4)) return false;
        for (int y = 0; y < height; ++y) {
            const std::uint8_t* src = &frame.plane[0][0] + static_cast<std::size_t>(y) * frame.stride[0];
            std::uint32_t* dst = &out[0] + static_cast<std::size_t>(y) * width;
            for (int x = 0; x < width; ++x) {
                const std::size_t i = static_cast<std::size_t>(x) * 4;
                dst[x] = (static_cast<std::uint32_t>(src[i + 0]) << 24) |
                         (static_cast<std::uint32_t>(src[i + 1]) << 16) |
                         (static_cast<std::uint32_t>(src[i + 2]) << 8) |
                         static_cast<std::uint32_t>(src[i + 3]);
            }
        }
        return true;
    }

    const bool planar = frame.format == PixelFormat_YUV420P;
    const bool nv12 = frame.format == PixelFormat_NV12;
    if (!planar && !nv12) return false;

    const int chroma_width = (width + 1) >> 1;
    const int chroma_height = (height + 1) >> 1;
    if (!has_plane_bytes(frame.plane[0], frame.stride[0], height, width)) return false;
    if (planar) {
        if (!has_plane_bytes(frame.plane[1], frame.stride[1], chroma_height, chroma_width) ||
            !has_plane_bytes(frame.plane[2], frame.stride[2], chroma_height, chroma_width)) return false;
    } else {
        if (!has_plane_bytes(frame.plane[1], frame.stride[1], chroma_height, chroma_width * 2)) return false;
    }

    /* Hot Xbox path: process one chroma sample into a 2x2 luma block and write
       four pixels directly. This removes the inner dx/dy loops and repeated
       vector indexing from the previous converter. */
    for (int cy = 0; cy < chroma_height; ++cy) {
        const int y0 = cy << 1;
        const int y1 = y0 + 1;
        const std::uint8_t* yrow0 = &frame.plane[0][0] + static_cast<std::size_t>(y0) * frame.stride[0];
        const std::uint8_t* yrow1 = y1 < height ? (&frame.plane[0][0] + static_cast<std::size_t>(y1) * frame.stride[0]) : 0;
        std::uint32_t* out0 = &out[0] + static_cast<std::size_t>(y0) * width;
        std::uint32_t* out1 = y1 < height ? (&out[0] + static_cast<std::size_t>(y1) * width) : 0;
        const std::uint8_t* urow = planar ? (&frame.plane[1][0] + static_cast<std::size_t>(cy) * frame.stride[1]) : 0;
        const std::uint8_t* vrow = planar ? (&frame.plane[2][0] + static_cast<std::size_t>(cy) * frame.stride[2]) : 0;
        const std::uint8_t* uvrow = nv12 ? (&frame.plane[1][0] + static_cast<std::size_t>(cy) * frame.stride[1]) : 0;

        for (int cx = 0; cx < chroma_width; ++cx) {
            const int x0 = cx << 1;
            const int x1 = x0 + 1;
            int uu, vv;
            if (planar) {
                uu = urow[cx];
                vv = vrow[cx];
            } else {
                uu = uvrow[cx * 2];
                vv = uvrow[cx * 2 + 1];
            }
            const int d = uu - 128;
            const int e = vv - 128;
            const int red_chroma = 409 * e + 128;
            const int green_chroma = -100 * d - 208 * e + 128;
            const int blue_chroma = 516 * d + 128;

            out0[x0] = pack_yuv(yrow0[x0], red_chroma, green_chroma, blue_chroma);
            if (x1 < width) out0[x1] = pack_yuv(yrow0[x1], red_chroma, green_chroma, blue_chroma);
            if (out1) {
                out1[x0] = pack_yuv(yrow1[x0], red_chroma, green_chroma, blue_chroma);
                if (x1 < width) out1[x1] = pack_yuv(yrow1[x1], red_chroma, green_chroma, blue_chroma);
            }
        }
    }
    return true;
}

} // namespace opennow
