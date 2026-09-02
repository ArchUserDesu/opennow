#include "opennow/video_decoder.hpp"

#ifdef OPENNOW_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavcodec/version.h>
#include <libavutil/mem.h>
}

#include <cstring>
#include <limits>

namespace opennow {
namespace {

class FFmpegH264Decoder : public VideoDecoder {
public:
    FFmpegH264Decoder() : ctx_(NULL), frame_(NULL) {}
private:
    FFmpegH264Decoder(const FFmpegH264Decoder&);
    FFmpegH264Decoder& operator=(const FFmpegH264Decoder&);
public:
    ~FFmpegH264Decoder() {
        free_frame();
        free_context();
    }

    bool open(int width, int height, int fps) {
        (void)fps;
        // Allow a failed/restarted stream to reopen the decoder cleanly.
        free_frame();
        free_context();
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (!codec) {
            return false;
        }

        ctx_ = avcodec_alloc_context3(codec);
        if (!ctx_) {
            return false;
        }

        // Cloud gaming wants the newest frame, not a deep playback buffer.
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
        ctx_->thread_count = 2;
        ctx_->thread_type = FF_THREAD_SLICE;
        ctx_->width = width;
        ctx_->height = height;

#if LIBAVCODEC_VERSION_MAJOR < 55
        frame_ = avcodec_alloc_frame();
#else
        frame_ = av_frame_alloc();
#endif
        if (!frame_) {
            free_context();
            return false;
        }

        if (avcodec_open2(ctx_, codec, NULL) < 0) {
            free_frame();
            free_context();
            return false;
        }
        return true;
    }

    bool decode(const std::uint8_t* data, std::size_t size, VideoFrame& out) {
        if (!ctx_ || !frame_ || !data || size == 0 ||
            size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }

        AVPacket packet; std::memset(&packet, 0, sizeof(packet));
        packet.data = const_cast<std::uint8_t*>(data);
        packet.size = static_cast<int>(size);

#if LIBAVCODEC_VERSION_MAJOR < 57
        int got_frame = 0;
        const int rc = avcodec_decode_video2(ctx_, frame_, &got_frame, &packet);
        if (rc < 0 || !got_frame) return false;
#else
        if (avcodec_send_packet(ctx_, &packet) < 0) return false;
        const int rc = avcodec_receive_frame(ctx_, frame_);
        if (rc < 0) return false;
#endif

        out.width = frame_->width;
        out.height = frame_->height;
        out.pts = frame_->pts;

        if (frame_->format == AV_PIX_FMT_YUV420P) {
            out.format = PixelFormat_YUV420P;
            for (int plane = 0; plane < 3; ++plane) {
                const int plane_height = plane == 0 ? out.height : (out.height + 1) / 2;
                const int plane_width = plane == 0 ? out.width : (out.width + 1) / 2;
                out.stride[plane] = plane_width;
                out.plane[plane].resize(
                    static_cast<std::size_t>(plane_width) * static_cast<std::size_t>(plane_height));
                for (int y = 0; y < plane_height; ++y) {
                    std::memcpy(
                        &out.plane[plane][0] + static_cast<std::size_t>(y) * plane_width,
                        frame_->data[plane] + static_cast<std::size_t>(y) * frame_->linesize[plane],
                        static_cast<std::size_t>(plane_width));
                }
            }
            return true;
        }

        if (frame_->format == AV_PIX_FMT_NV12) {
            out.format = PixelFormat_NV12;
            out.stride[0] = out.width;
            out.stride[1] = out.width;
            out.stride[2] = 0;
            out.plane[0].resize(static_cast<std::size_t>(out.width) * out.height);
            out.plane[1].resize(
                static_cast<std::size_t>(out.width) * static_cast<std::size_t>((out.height + 1) / 2));
            out.plane[2].clear();

            for (int y = 0; y < out.height; ++y) {
                std::memcpy(
                    &out.plane[0][0] + static_cast<std::size_t>(y) * out.width,
                    frame_->data[0] + static_cast<std::size_t>(y) * frame_->linesize[0],
                    static_cast<std::size_t>(out.width));
            }
            for (int y = 0; y < (out.height + 1) / 2; ++y) {
                std::memcpy(
                    &out.plane[1][0] + static_cast<std::size_t>(y) * out.width,
                    frame_->data[1] + static_cast<std::size_t>(y) * frame_->linesize[1],
                    static_cast<std::size_t>(out.width));
            }
            return true;
        }

        return false;
    }

    void flush() {
        if (ctx_) {
            avcodec_flush_buffers(ctx_);
        }
    }

private:
    void free_frame() {
        if (!frame_) return;
#if LIBAVCODEC_VERSION_MAJOR < 55
        av_free(frame_);
        frame_ = NULL;
#else
        av_frame_free(&frame_);
#endif
    }
    void free_context() {
        if (!ctx_) return;
#if LIBAVCODEC_VERSION_MAJOR < 55
        avcodec_close(ctx_);
        av_free(ctx_);
        ctx_ = NULL;
#else
        avcodec_free_context(&ctx_);
#endif
    }
    AVCodecContext* ctx_;
    AVFrame* frame_;
};

} // namespace

VideoDecoder* make_ffmpeg_h264_decoder() { return new FFmpegH264Decoder(); }

} // namespace opennow
#else
namespace opennow {
VideoDecoder* make_ffmpeg_h264_decoder() { return NULL; }
} // namespace opennow
#endif
