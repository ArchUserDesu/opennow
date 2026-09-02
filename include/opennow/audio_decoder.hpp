#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace opennow {
class AudioDecoder { public: virtual ~AudioDecoder(){}; virtual bool open(int sample_rate=48000,int channels=2)=0; virtual bool decode(const std::uint8_t* packet,std::size_t bytes,std::vector<std::int16_t>& pcm,int& frames)=0; virtual void reset()=0; };
AudioDecoder* make_opus_audio_decoder();
}
