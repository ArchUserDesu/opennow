#include "opennow/audio_decoder.hpp"
#include "opennow/logger.hpp"
extern "C" {
#include <opus.h>
}
namespace opennow {
namespace {
#if defined(OPENNOW_XDK)
void prepare_xdk_pcm_bytes(std::vector<std::int16_t>& pcm) {
    /* Xenon is big-endian.  XAudio2's Xbox 360 documentation explicitly warns
       that XAUDIO2_BUFFER sample data may need byte swapping on Xbox 360.
       The platform backend currently serializes the numeric int16 value as
       little-endian bytes, so swap the numeric value here; that makes the bytes
       handed to the Xbox source voice equal to the decoder's native big-endian
       PCM representation. */
    for (std::size_t i=0;i<pcm.size();++i) {
        const std::uint16_t u=(std::uint16_t)pcm[i];
        pcm[i]=(std::int16_t)((u>>8)|(u<<8));
    }
}
#else
void prepare_xdk_pcm_bytes(std::vector<std::int16_t>&) {}
#endif
}
class OpusAudioDecoder : public AudioDecoder {
public:
    OpusAudioDecoder():dec_(NULL),rate_(48000),channels_(2),decode_count_(0),decoded_frames_total_(0){}
    ~OpusAudioDecoder(){if(dec_)opus_decoder_destroy(dec_);}
    bool open(int sample_rate,int channels){
        if(dec_)opus_decoder_destroy(dec_);
        int err=OPUS_OK;rate_=sample_rate;channels_=channels;decode_count_=0;decoded_frames_total_=0;
        ON_LOGI("audio-decode","Opus open begin rate=%d channels=%d",sample_rate,channels);
        dec_=opus_decoder_create(sample_rate,channels,&err);
        if(!dec_||err!=OPUS_OK)ON_LOGE("audio-decode","opus_decoder_create failed err=%d text=%s",err,opus_strerror(err));
        else ON_LOGI("audio-decode","Opus open complete");
        return dec_&&err==OPUS_OK;
    }
    bool decode(const std::uint8_t* packet,std::size_t bytes,std::vector<std::int16_t>& pcm,int& frames){
        frames=0;if(!dec_||!packet||bytes==0){ON_LOGE("audio-decode","invalid decode input decoder=%p packet=%p bytes=%u",dec_,packet,(unsigned)bytes);return false;}
        const int kMaxFrames=5760;pcm.resize((std::size_t)kMaxFrames*(std::size_t)channels_);
        int n=opus_decode(dec_,packet,(opus_int32)bytes,&pcm[0],kMaxFrames,0);
        if(n<0){ON_LOGE("audio-decode","opus_decode failed err=%d text=%s packet_bytes=%u",n,opus_strerror(n),(unsigned)bytes);pcm.clear();return false;}
        frames=n;pcm.resize((std::size_t)n*(std::size_t)channels_);
        ++decode_count_;decoded_frames_total_+=(unsigned long long)n;
        if(decode_count_<=8||decode_count_%500==0){
            int mn=32767,mx=-32768;unsigned long long abs_sum=0;unsigned zero_cross=0;int prev=0;
            const std::size_t samples=pcm.size();
            for(std::size_t i=0;i<samples;++i){int v=(int)pcm[i];if(v<mn)mn=v;if(v>mx)mx=v;abs_sum+=(unsigned long long)(v<0?-v:v);if(i&&((v<0)!=(prev<0)))++zero_cross;prev=v;}
            const unsigned avg_abs=samples?(unsigned)(abs_sum/samples):0;
            const int l0=samples>0?(int)pcm[0]:0,r0=samples>1?(int)pcm[1]:0,l1=samples>2?(int)pcm[2]:0,r1=samples>3?(int)pcm[3]:0;
            ON_LOGI("audio-pcm","decode=%llu packet_bytes=%u frames=%d total_pcm_ms=%llu min=%d max=%d avg_abs=%u zero_cross=%u first_lr=%d,%d next_lr=%d,%d",decode_count_,(unsigned)bytes,n,(decoded_frames_total_*1000ULL)/(unsigned long long)rate_,mn,mx,avg_abs,zero_cross,l0,r0,l1,r1);
        }
#if defined(OPENNOW_XDK)
        if(decode_count_==1)ON_LOGI("audio-pcm-byteorder","bridge=opus_native_s16be_to_xaudio360 byteswap_before_le_serializer samples=%u",(unsigned)pcm.size());
#endif
        prepare_xdk_pcm_bytes(pcm);
        return true;
    }
    bool conceal(int frame_count,std::vector<std::int16_t>& pcm,int& frames){
        frames=0;if(!dec_||frame_count<=0)return false;
        pcm.resize((std::size_t)frame_count*(std::size_t)channels_);
        int n=opus_decode(dec_,NULL,0,&pcm[0],frame_count,0);
        if(n<0){ON_LOGE("audio-decode","opus PLC failed err=%d text=%s",n,opus_strerror(n));pcm.clear();return false;}
        frames=n;pcm.resize((std::size_t)n*(std::size_t)channels_);prepare_xdk_pcm_bytes(pcm);return true;
    }
    void reset(){if(dec_){ON_LOGW("audio-decode","Opus decoder state reset after decode_count=%llu total_pcm_ms=%llu",decode_count_,(decoded_frames_total_*1000ULL)/(unsigned long long)rate_);opus_decoder_ctl(dec_,OPUS_RESET_STATE);}}
private:
    OpusDecoder* dec_;int rate_,channels_;unsigned long long decode_count_,decoded_frames_total_;
};
AudioDecoder* make_opus_audio_decoder(){return new OpusAudioDecoder();}
}