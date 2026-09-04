#include "opennow/video_decoder.hpp"
#include "opennow/logger.hpp"

#ifdef OPENNOW_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#if LIBAVCODEC_VERSION_MAJOR >= 55
#include <libavutil/frame.h>
#endif
#include <libavutil/pixfmt.h>
#include <libavutil/mem.h>
}

#include <cstring>
#include <limits>

namespace opennow {
namespace {

extern "C" AVCodec ff_h264_decoder;

void register_h264_decoder() {
    static bool registered = false;
    if (!registered) {
        avcodec_register(&ff_h264_decoder);
        registered = true;
        ON_LOGI("video-decode", "FFmpeg H.264 decoder registered explicitly");
    }
}

class FFmpegH264Decoder : public VideoDecoder {
public:
    FFmpegH264Decoder() : ctx_(NULL), frame_(NULL), decoded_count_(0) {}
private:
    FFmpegH264Decoder(const FFmpegH264Decoder&);
    FFmpegH264Decoder& operator=(const FFmpegH264Decoder&);
public:
    ~FFmpegH264Decoder() { free_frame(); free_context(); }

    bool open(int width, int height, int fps) {
        (void)fps;
        free_frame(); free_context(); decoded_count_=0; register_h264_decoder();
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        ON_LOGI("video-decode", "FFmpeg H.264 open begin version=%u target=%dx%d fps=%d",(unsigned)LIBAVCODEC_VERSION_MAJOR,width,height,fps);
        if (!codec) { ON_LOGE("video-decode","avcodec_find_decoder H264 returned null"); return false; }
        ctx_ = avcodec_alloc_context3(codec);
        ON_LOGD("video-decode", "context allocation returned ptr=%p",ctx_);
        if(!ctx_) { ON_LOGE("video-decode","avcodec_alloc_context3 failed"); return false; }
#if LIBAVCODEC_VERSION_MAJOR < 56
        ctx_->flags |= CODEC_FLAG_LOW_DELAY;
        ctx_->flags2 |= CODEC_FLAG2_FAST;
#else
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
#endif
        const int requested_thread_count=4;
        const int requested_thread_type=FF_THREAD_SLICE;
        ctx_->thread_count=requested_thread_count;
        ctx_->thread_type=requested_thread_type;
        /* The latest runtime log showed normal deblocking pushing 720p decode
           to 51-72 ms/frame and causing thousands of access-unit drops.  Keep
           the dedicated decoder worker but restore the proven fast Xenon path. */
        ctx_->skip_loop_filter=AVDISCARD_ALL;
        ctx_->width=width; ctx_->height=height;
#if LIBAVCODEC_VERSION_MAJOR < 55
        frame_=avcodec_alloc_frame();
#else
        frame_=av_frame_alloc();
#endif
        ON_LOGD("video-decode","frame allocation returned ptr=%p",frame_);
        if(!frame_){ON_LOGE("video-decode","frame allocation failed");free_context();return false;}
        ON_LOGI("video-decode","avcodec_open2 begin codec=%s requested_threads=%d requested_type=%d",codec->name?codec->name:"<unknown>",requested_thread_count,requested_thread_type);
        int open_rc=avcodec_open2(ctx_,codec,NULL);
        if(open_rc<0){ON_LOGE("video-decode","avcodec_open2 failed rc=%d",open_rc);free_frame();free_context();return false;}
        const int caps=codec->capabilities;
        ON_LOGI("video-decode","H264 threading requested=%d type=%d actual=%d active=%d caps=0x%08x caps_slice=%d caps_frame=%d",requested_thread_count,requested_thread_type,ctx_->thread_count,ctx_->active_thread_type,(unsigned)caps,(caps&CODEC_CAP_SLICE_THREADS)?1:0,(caps&CODEC_CAP_FRAME_THREADS)?1:0);
        ON_LOGI("video-decode","FFmpeg H.264 open complete threads=%d type=%d active=%d skip_loop_filter=%d",ctx_->thread_count,ctx_->thread_type,ctx_->active_thread_type,(int)ctx_->skip_loop_filter);
        return true;
    }

    bool decode(const std::uint8_t* data,std::size_t size,VideoFrame& out) {
        if(!ctx_||!frame_||!data||size==0||size>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
        AVPacket packet;std::memset(&packet,0,sizeof(packet));packet.data=const_cast<std::uint8_t*>(data);packet.size=static_cast<int>(size);
#if LIBAVCODEC_VERSION_MAJOR < 57
        int got_frame=0;
        const int rc=avcodec_decode_video2(ctx_,frame_,&got_frame,&packet);
        if(rc<0||!got_frame){if(rc<0)ON_LOGE("video-decode","decode failed rc=%d packet_bytes=%u",rc,(unsigned)size);return false;}
#else
        if(avcodec_send_packet(ctx_,&packet)<0)return false;
        const int rc=avcodec_receive_frame(ctx_,frame_);
        if(rc<0)return false;
#endif
        ++decoded_count_; out.width=frame_->width;out.height=frame_->height;out.pts=frame_->pts;
        if(decoded_count_<=4||decoded_count_%300==0){
            ON_LOGI("video-frame","decoded=%llu coded_bytes=%u actual=%dx%d format=%d linesize=%d,%d,%d range=%s",decoded_count_,(unsigned)size,frame_->width,frame_->height,frame_->format,frame_->linesize[0],frame_->linesize[1],frame_->linesize[2],frame_->format==AV_PIX_FMT_YUVJ420P?"full":"limited");
        }
        if(frame_->format==AV_PIX_FMT_YUV420P||frame_->format==AV_PIX_FMT_YUVJ420P){
            out.format=frame_->format==AV_PIX_FMT_YUVJ420P?PixelFormat_YUV420P_FULL:PixelFormat_YUV420P;
            for(int plane=0;plane<3;++plane){
                const int plane_height=plane==0?out.height:(out.height+1)/2;
                const int plane_width=plane==0?out.width:(out.width+1)/2;
                out.stride[plane]=plane_width;
                out.plane[plane].resize(static_cast<std::size_t>(plane_width)*static_cast<std::size_t>(plane_height));
                for(int y=0;y<plane_height;++y)std::memcpy(&out.plane[plane][0]+static_cast<std::size_t>(y)*plane_width,frame_->data[plane]+static_cast<std::size_t>(y)*frame_->linesize[plane],static_cast<std::size_t>(plane_width));
            }
            return true;
        }
        if(frame_->format==AV_PIX_FMT_NV12){
            out.format=PixelFormat_NV12;out.stride[0]=out.width;out.stride[1]=out.width;out.stride[2]=0;
            out.plane[0].resize(static_cast<std::size_t>(out.width)*out.height);
            out.plane[1].resize(static_cast<std::size_t>(out.width)*static_cast<std::size_t>((out.height+1)/2));out.plane[2].clear();
            for(int y=0;y<out.height;++y)std::memcpy(&out.plane[0][0]+static_cast<std::size_t>(y)*out.width,frame_->data[0]+static_cast<std::size_t>(y)*frame_->linesize[0],static_cast<std::size_t>(out.width));
            for(int y=0;y<(out.height+1)/2;++y)std::memcpy(&out.plane[1][0]+static_cast<std::size_t>(y)*out.width,frame_->data[1]+static_cast<std::size_t>(y)*frame_->linesize[1],static_cast<std::size_t>(out.width));
            return true;
        }
        ON_LOGE("video-decode","unsupported pixel format=%d size=%dx%d",frame_->format,out.width,out.height);return false;
    }

    void flush(){if(ctx_)avcodec_flush_buffers(ctx_);}
private:
    void free_frame(){if(!frame_)return;
#if LIBAVCODEC_VERSION_MAJOR < 55
        av_free(frame_);frame_=NULL;
#else
        av_frame_free(&frame_);
#endif
    }
    void free_context(){if(!ctx_)return;
#if LIBAVCODEC_VERSION_MAJOR < 55
        avcodec_close(ctx_);av_free(ctx_);ctx_=NULL;
#else
        avcodec_free_context(&ctx_);
#endif
    }
    AVCodecContext* ctx_;AVFrame* frame_;unsigned long long decoded_count_;
};
}
VideoDecoder* make_ffmpeg_h264_decoder(){return new FFmpegH264Decoder();}
}
#else
namespace opennow { VideoDecoder* make_ffmpeg_h264_decoder(){return NULL;} }
#endif