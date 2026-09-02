#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace opennow {
struct RtpPacketView { std::uint16_t sequence; std::uint32_t timestamp; bool marker; const std::uint8_t* payload; std::size_t payload_size; RtpPacketView():sequence(0),timestamp(0),marker(false),payload(NULL),payload_size(0){} };
struct H264AccessUnit { std::uint32_t timestamp; bool idr; std::vector<std::uint8_t> annexb; H264AccessUnit():timestamp(0),idr(false){} };
class H264RtpDepacketizer {
 public: H264RtpDepacketizer(); bool push(const RtpPacketView& p,H264AccessUnit& completed); void reset(); std::size_t dropped()const{return dropped_;}
 private: void add_start_code(); void add_nal(const std::uint8_t*,std::size_t); bool flush(H264AccessUnit&);
 std::vector<std::uint8_t> au_; std::uint32_t ts_; std::uint16_t expected_; bool have_seq_,have_ts_,fu_open_,idr_; std::size_t dropped_;
};
}
