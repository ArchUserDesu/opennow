#include "opennow/audio_decoder.hpp"
extern "C" {
#include <opus/opus.h>
}
namespace opennow {
class OpusAudioDecoder : public AudioDecoder {
public:
    OpusAudioDecoder():dec_(NULL),rate_(48000),channels_(2){}
    ~OpusAudioDecoder(){if(dec_)opus_decoder_destroy(dec_);}
    bool open(int sample_rate,int channels){if(dec_)opus_decoder_destroy(dec_);int err=OPUS_OK;rate_=sample_rate;channels_=channels;dec_=opus_decoder_create(sample_rate,channels,&err);return dec_&&err==OPUS_OK;}
    bool decode(const std::uint8_t* packet,std::size_t bytes,std::vector<std::int16_t>& pcm,int& frames){frames=0;if(!dec_||!packet||bytes==0)return false;const int kMaxFrames=5760;pcm.resize((std::size_t)kMaxFrames*(std::size_t)channels_);int n=opus_decode(dec_,packet,(opus_int32)bytes,&pcm[0],kMaxFrames,0);if(n<0){pcm.clear();return false;}frames=n;pcm.resize((std::size_t)n*(std::size_t)channels_);return true;}
    void reset(){if(dec_)opus_decoder_ctl(dec_,OPUS_RESET_STATE);}
private: OpusDecoder* dec_;int rate_,channels_;
};
AudioDecoder* make_opus_audio_decoder(){return new OpusAudioDecoder();}
}
