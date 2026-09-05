#include "opennow/video_decoder.hpp"
#include "opennow/logger.hpp"

#ifdef OPENNOW_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/version.h>
#if LIBAVCODEC_VERSION_MAJOR >= 55
#include <libavutil/frame.h>
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 57
#include <libavutil/error.h>
#endif
#include <libavutil/pixfmt.h>
#include <libavutil/mem.h>
}

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(OPENNOW_XDK)
#include <xtl.h>
extern "C" int ptw32_processInitialize(void);
#endif

namespace opennow {
namespace {

std::uint64_t decoder_now_us() {
#if defined(OPENNOW_XDK)
    return static_cast<std::uint64_t>(GetTickCount()) * 1000ULL;
#else
    return 0;
#endif
}

void diagnostic_av_log(void*, int level, const char* format, va_list args) {
    if (level > AV_LOG_INFO) return;
    char message[768];
#if defined(OPENNOW_XDK)
    _vsnprintf(message, sizeof(message) - 1, format, args);
#else
    std::vsnprintf(message, sizeof(message) - 1, format, args);
#endif
    message[sizeof(message) - 1] = '\0';
    std::size_t n=std::strlen(message);while(n&&(message[n-1]=='\n'||message[n-1]=='\r'))message[--n]='\0';
    log_message(level<=AV_LOG_ERROR?LogError:(level<=AV_LOG_WARNING?LogWarn:LogInfo),"ffmpeg", "%s",message);
}

extern "C" AVCodec ff_h264_decoder;

bool ensure_xdk_pthreads_initialized() {
#if defined(OPENNOW_XDK)
    static bool initialized = false;
    if (!initialized) {
        ON_LOGI("xdk-pthread", "ptw32_processInitialize begin");
        const int ok = ptw32_processInitialize();
        ON_LOGI("xdk-pthread", "ptw32_processInitialize returned=%d", ok);
        if (!ok) {
            ON_LOGE("xdk-pthread", "pthread process initialization failed; refusing threaded FFmpeg startup");
            return false;
        }
        initialized = true;
    }
#endif
    return true;
}

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
    FFmpegH264Decoder() : ctx_(NULL), frame_(NULL), submitted_count_(0), decoded_count_(0), no_frame_count_(0), error_count_(0),codec_us_(0),copy_us_(0),codec_max_us_(0),copy_max_us_(0),over_16ms_(0),over_33ms_(0),over_50ms_(0),started_us_(0) {}
private:
    FFmpegH264Decoder(const FFmpegH264Decoder&);
    FFmpegH264Decoder& operator=(const FFmpegH264Decoder&);
public:
    ~FFmpegH264Decoder() { free_frame(); free_context(); }

    bool open(int width, int height, int fps) {
        free_frame(); free_context(); submitted_count_=0;decoded_count_=0;no_frame_count_=0;error_count_=0;codec_us_=copy_us_=codec_max_us_=copy_max_us_=over_16ms_=over_33ms_=over_50ms_=0;started_us_=decoder_now_us();
        if (!ensure_xdk_pthreads_initialized()) return false;
        av_log_set_level(AV_LOG_INFO);av_log_set_callback(&diagnostic_av_log);
        register_h264_decoder();
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        ON_LOGI("video-decode", "FFmpeg H.264 open begin version=%u major=%u target=%dx%d fps=%d",(unsigned)avcodec_version(),(unsigned)LIBAVCODEC_VERSION_MAJOR,width,height,fps);
        if (!codec) { ON_LOGE("video-decode","avcodec_find_decoder H264 returned null"); return false; }
        ctx_ = avcodec_alloc_context3(codec);
        ON_LOGD("video-decode", "context allocation returned ptr=%p",ctx_);
        if(!ctx_) { ON_LOGE("video-decode","avcodec_alloc_context3 failed"); return false; }

#if LIBAVCODEC_VERSION_MAJOR < 56
#if defined(OPENNOW_XDK)
        ctx_->flags &= ~CODEC_FLAG_LOW_DELAY;
#else
        ctx_->flags |= CODEC_FLAG_LOW_DELAY;
#endif
        ctx_->flags2 |= CODEC_FLAG2_FAST;
#else
#if defined(OPENNOW_XDK)
        ctx_->flags &= ~AV_CODEC_FLAG_LOW_DELAY;
#else
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
#endif
        ctx_->flags2 |= AV_CODEC_FLAG2_FAST;
#endif

#if defined(OPENNOW_XDK)
        /* FFmpeg frame threading pipelines complete H.264 frames. Start with
           three workers, one per physical Xenon core, matching Switch's
           software worker count. LOW_DELAY must remain clear or FFmpeg 1.2
           will silently refuse FF_THREAD_FRAME. */
        const int requested_thread_count=3;
        const int requested_thread_type=FF_THREAD_FRAME;
#else
        const int requested_thread_count=4;
        const int requested_thread_type=FF_THREAD_SLICE;
#endif
        ctx_->thread_count=requested_thread_count;
        ctx_->thread_type=requested_thread_type;
        ctx_->skip_loop_filter=AVDISCARD_ALL;
        ctx_->width=width; ctx_->height=height;

#if LIBAVCODEC_VERSION_MAJOR < 56
        const int low_delay_enabled=(ctx_->flags&CODEC_FLAG_LOW_DELAY)?1:0;
        const int fast_enabled=(ctx_->flags2&CODEC_FLAG2_FAST)?1:0;
#else
        const int low_delay_enabled=(ctx_->flags&AV_CODEC_FLAG_LOW_DELAY)?1:0;
        const int fast_enabled=(ctx_->flags2&AV_CODEC_FLAG2_FAST)?1:0;
#endif
        ON_LOGI("video-decode","decoder mode requested_threads=%d requested_type=%d frame_threads=%d slice_threads=%d low_delay=%d fast=%d skip_loop_filter=%d",requested_thread_count,requested_thread_type,requested_thread_type==FF_THREAD_FRAME?1:0,requested_thread_type==FF_THREAD_SLICE?1:0,low_delay_enabled,fast_enabled,(int)ctx_->skip_loop_filter);

#if LIBAVCODEC_VERSION_MAJOR < 55
        frame_=avcodec_alloc_frame();
#else
        frame_=av_frame_alloc();
#endif
        ON_LOGD("video-decode","frame allocation returned ptr=%p",frame_);
        if(!frame_){ON_LOGE("video-decode","frame allocation failed");free_context();return false;}
        ON_LOGI("video-decode","avcodec_open2 begin codec=%s requested_threads=%d requested_type=%d",codec->name?codec->name:"<unknown>",requested_thread_count,requested_thread_type);
        int open_rc=avcodec_open2(ctx_,codec,NULL);
        ON_LOGI("video-decode","avcodec_open2 returned rc=%d",open_rc);
        if(open_rc<0){ON_LOGE("video-decode","avcodec_open2 failed rc=%d",open_rc);free_frame();free_context();return false;}
        const int caps=codec->capabilities;
#if LIBAVCODEC_VERSION_MAJOR < 56
        const int caps_slice=(caps&CODEC_CAP_SLICE_THREADS)?1:0;
        const int caps_frame=(caps&CODEC_CAP_FRAME_THREADS)?1:0;
#else
        const int caps_slice=(caps&AV_CODEC_CAP_SLICE_THREADS)?1:0;
        const int caps_frame=(caps&AV_CODEC_CAP_FRAME_THREADS)?1:0;
#endif
        ON_LOGI("video-decode","H264 threading requested=%d type=%d actual=%d configured_type=%d active=%d caps=0x%08x caps_slice=%d caps_frame=%d low_delay=%d fast=%d skip_loop_filter=%d",requested_thread_count,requested_thread_type,ctx_->thread_count,ctx_->thread_type,ctx_->active_thread_type,(unsigned)caps,caps_slice,caps_frame,low_delay_enabled,fast_enabled,(int)ctx_->skip_loop_filter);
        if(ctx_->active_thread_type!=requested_thread_type){ON_LOGE("video-decode","requested threading did not activate requested_type=%d active_thread_type=%d",requested_thread_type,ctx_->active_thread_type);free_frame();free_context();return false;}
        ON_LOGI("video-decode","FFmpeg H.264 open complete threads=%d type=%d active=%d skip_loop_filter=%d",ctx_->thread_count,ctx_->thread_type,ctx_->active_thread_type,(int)ctx_->skip_loop_filter);
        return true;
    }

    VideoDecodeResult decode(const std::uint8_t* data,std::size_t size,VideoFrame& out) {
        if(!ctx_||!frame_||!data||size==0||size>static_cast<std::size_t>(std::numeric_limits<int>::max())){++error_count_;return VideoDecodeError;}
        ++submitted_count_;
        if(submitted_count_==1)ON_LOGI("video-decode","first H264 access unit decode begin bytes=%u",(unsigned)size);
        AVPacket packet;std::memset(&packet,0,sizeof(packet));packet.data=const_cast<std::uint8_t*>(data);packet.size=static_cast<int>(size);const std::uint64_t codec_began=decoder_now_us();
#if LIBAVCODEC_VERSION_MAJOR < 57
        int got_frame=0;
        const int rc=avcodec_decode_video2(ctx_,frame_,&got_frame,&packet);
        if(submitted_count_==1)ON_LOGI("video-decode","first H264 access unit decode returned rc=%d got_frame=%d",rc,got_frame);
        const std::uint64_t codec_done=decoder_now_us();const std::uint64_t codec_delta=codec_done-codec_began;codec_us_+=codec_delta;if(codec_delta>codec_max_us_)codec_max_us_=codec_delta;if(codec_delta>=16000ULL)++over_16ms_;if(codec_delta>=33000ULL)++over_33ms_;if(codec_delta>=50000ULL)++over_50ms_;
        if(rc<0){++error_count_;ON_LOGE("video-decode","decode failed rc=%d codec_ms=%llu packet_bytes=%u submitted=%llu errors=%llu",rc,codec_delta/1000ULL,(unsigned)size,submitted_count_,error_count_);return VideoDecodeError;}
        if(!got_frame){++no_frame_count_;if(no_frame_count_<=4||no_frame_count_%300==0)ON_LOGI("video-decode","packet accepted without output submitted=%llu no_frame=%llu frames=%llu codec_ms=%llu packet_bytes=%u",submitted_count_,no_frame_count_,decoded_count_,codec_delta/1000ULL,(unsigned)size);return VideoDecodeNoFrame;}
#else
        const int send_rc=avcodec_send_packet(ctx_,&packet);
        if(send_rc<0){++error_count_;ON_LOGE("video-decode","send packet failed rc=%d packet_bytes=%u submitted=%llu errors=%llu",send_rc,(unsigned)size,submitted_count_,error_count_);return VideoDecodeError;}
        const int rc=avcodec_receive_frame(ctx_,frame_);
        if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF){++no_frame_count_;if(no_frame_count_<=4||no_frame_count_%300==0)ON_LOGI("video-decode","packet accepted without output submitted=%llu no_frame=%llu frames=%llu packet_bytes=%u",submitted_count_,no_frame_count_,decoded_count_,(unsigned)size);return VideoDecodeNoFrame;}
        if(rc<0){++error_count_;ON_LOGE("video-decode","receive frame failed rc=%d packet_bytes=%u submitted=%llu errors=%llu",rc,(unsigned)size,submitted_count_,error_count_);return VideoDecodeError;}
#endif
        if(frame_->decode_error_flags){++error_count_;ON_LOGW("video-decode","discarding damaged output flags=%d submitted=%llu",frame_->decode_error_flags,submitted_count_);return VideoDecodeError;}
        ++decoded_count_; out.width=frame_->width;out.height=frame_->height;out.pts=frame_->pts;const std::uint64_t copy_began=decoder_now_us();
        if(decoded_count_<=4||decoded_count_%300==0){
            ON_LOGI("video-frame","decoded=%llu submitted=%llu no_frame=%llu errors=%llu coded_bytes=%u actual=%dx%d format=%d linesize=%d,%d,%d range=%s",decoded_count_,submitted_count_,no_frame_count_,error_count_,(unsigned)size,frame_->width,frame_->height,frame_->format,frame_->linesize[0],frame_->linesize[1],frame_->linesize[2],frame_->format==AV_PIX_FMT_YUVJ420P?"full":"limited");
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
            const std::uint64_t d=decoder_now_us()-copy_began;copy_us_+=d;if(d>copy_max_us_)copy_max_us_=d;log_summary();return VideoDecodeFrame;
        }
        if(frame_->format==AV_PIX_FMT_NV12){
            out.format=PixelFormat_NV12;out.stride[0]=out.width;out.stride[1]=out.width;out.stride[2]=0;
            out.plane[0].resize(static_cast<std::size_t>(out.width)*out.height);
            out.plane[1].resize(static_cast<std::size_t>(out.width)*static_cast<std::size_t>((out.height+1)/2));out.plane[2].clear();
            for(int y=0;y<out.height;++y)std::memcpy(&out.plane[0][0]+static_cast<std::size_t>(y)*out.width,frame_->data[0]+static_cast<std::size_t>(y)*frame_->linesize[0],static_cast<std::size_t>(out.width));
            for(int y=0;y<(out.height+1)/2;++y)std::memcpy(&out.plane[1][0]+static_cast<std::size_t>(y)*out.width,frame_->data[1]+static_cast<std::size_t>(y)*frame_->linesize[1],static_cast<std::size_t>(out.width));
            const std::uint64_t d=decoder_now_us()-copy_began;copy_us_+=d;if(d>copy_max_us_)copy_max_us_=d;log_summary();return VideoDecodeFrame;
        }
        ++error_count_;ON_LOGE("video-decode","unsupported pixel format=%d size=%dx%d submitted=%llu errors=%llu",frame_->format,out.width,out.height,submitted_count_,error_count_);return VideoDecodeError;
    }

    void flush(){if(ctx_){ON_LOGI("video-decode","flush submitted=%llu frames=%llu no_frame=%llu errors=%llu",submitted_count_,decoded_count_,no_frame_count_,error_count_);avcodec_flush_buffers(ctx_);}}
private:
    void log_summary(){if(decoded_count_==1||decoded_count_%60==0){const std::uint64_t elapsed=decoder_now_us()-started_us_;ON_LOGI("video-codec-perf","frames=%llu submitted=%llu no_frame=%llu errors=%llu elapsed_ms=%llu output_fps_x10=%llu codec_avg_ms_x10=%llu codec_max_ms=%llu copy_avg_ms_x10=%llu copy_max_ms=%llu codec_ge16=%llu ge33=%llu ge50=%llu",decoded_count_,submitted_count_,no_frame_count_,error_count_,elapsed/1000ULL,elapsed?decoded_count_*10000000ULL/elapsed:0,submitted_count_?codec_us_*10ULL/submitted_count_/1000ULL:0,codec_max_us_/1000ULL,decoded_count_?copy_us_*10ULL/decoded_count_/1000ULL:0,copy_max_us_/1000ULL,over_16ms_,over_33ms_,over_50ms_);}}
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
    AVCodecContext* ctx_;AVFrame* frame_;unsigned long long submitted_count_,decoded_count_,no_frame_count_,error_count_,codec_us_,copy_us_,codec_max_us_,copy_max_us_,over_16ms_,over_33ms_,over_50ms_,started_us_;
};
}
VideoDecoder* make_ffmpeg_h264_decoder(){return new FFmpegH264Decoder();}
}
#else
namespace opennow { VideoDecoder* make_ffmpeg_h264_decoder(){return NULL;} }
#endif
