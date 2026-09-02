#pragma once
#include "bounded_queue.hpp"
#include "video_decoder.hpp"
#include "xenon_platform.hpp"
#include <cstdint>
#include <vector>
namespace opennow { struct EncodedAccessUnit{std::vector<std::uint8_t> bytes;bool idr;std::uint32_t rtp_timestamp;EncodedAccessUnit():idr(false),rtp_timestamp(0){}};class StreamPump{public:StreamPump(XenonPlatform& p,VideoDecoder& d):platform_(p),decoder_(d),q_(1){}bool enqueue(EncodedAccessUnit u){return q_.push(u);}bool step();std::size_t dropped()const{return q_.drops();}private:XenonPlatform& platform_;VideoDecoder& decoder_;BoundedQueue<EncodedAccessUnit> q_;};}
