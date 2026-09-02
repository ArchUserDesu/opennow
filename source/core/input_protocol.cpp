// Packet layout follows OpenNOW-Switch's MIT-licensed GFN input implementation.
#include "opennow/input_protocol.hpp"
#include "opennow/endian.hpp"
#include <algorithm>
namespace opennow {
static std::int16_t axis(float v){v=std::max(-1.0f,std::min(1.0f,v));return (std::int16_t)(v*32767.0f);}
std::vector<std::uint8_t> build_gamepad_payload(std::uint64_t ts,const GamepadState& s){
 std::vector<std::uint8_t> p; p.reserve(38); put_u32_le(p,12); put_u16_le(p,26); put_u16_le(p,s.controller_id);
 put_u16_le(p,s.controller_bitmap); put_u16_le(p,20); put_u16_le(p,s.buttons);
 put_u16_le(p,(std::uint16_t)(s.left_trigger | (std::uint16_t(s.right_trigger)<<8)));
 put_i16_le(p,axis(s.lx));put_i16_le(p,axis(s.ly));put_i16_le(p,axis(s.rx));put_i16_le(p,axis(s.ry));
 put_u16_le(p,0);put_u16_le(p,85);put_u16_le(p,0);put_u64_le(p,ts);return p;
}
std::vector<std::uint8_t> wrap_reliable_gamepad(int ver,std::uint64_t ts,const std::vector<std::uint8_t>& p){if(ver<=2)return p;std::vector<std::uint8_t> o;o.reserve(12+p.size());o.push_back(0x23);put_u64_be(o,ts);o.push_back(0x21);put_u16_be(o,(std::uint16_t)p.size());o.insert(o.end(),p.begin(),p.end());return o;}
std::vector<std::uint8_t> wrap_partial_gamepad(int ver,std::uint64_t ts,std::uint8_t cid,std::uint16_t seq,const std::vector<std::uint8_t>& p){if(ver<=2)return p;std::vector<std::uint8_t> o;o.reserve(16+p.size());o.push_back(0x23);put_u64_be(o,ts);o.push_back(0x26);o.push_back(cid);put_u16_be(o,seq);o.push_back(0x21);put_u16_be(o,(std::uint16_t)p.size());o.insert(o.end(),p.begin(),p.end());return o;}
bool input_encoding_self_test(){GamepadState s;s.controller_id=2;s.controller_bitmap=0x0f0f;s.buttons=0x1234;s.left_trigger=0x56;s.right_trigger=0x78;s.lx=1.0f/32767.0f;s.ly=-2.0f/32767.0f;s.rx=3.0f/32767.0f;s.ry=-4.0f/32767.0f;auto raw=build_gamepad_payload(0x0102030405060708ULL,s);if(raw.size()!=38||raw[0]!=12||raw[6]!=2||raw[8]!=0x0f||raw[9]!=0x0f||raw[26]!=0x55||raw[30]!=0x08||raw[37]!=0x01)return false;auto r=wrap_reliable_gamepad(3,0x0102030405060708ULL,raw);auto q=wrap_partial_gamepad(3,0x0102030405060708ULL,2,1,raw);return r.size()==50&&r[0]==0x23&&r[9]==0x21&&r[10]==0&&r[11]==38&&q.size()==54&&q[9]==0x26&&q[10]==2&&q[13]==0x21;}
}
