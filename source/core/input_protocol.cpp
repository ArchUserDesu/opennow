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
std::vector<std::uint8_t> build_mouse_button_payload(std::uint64_t ts,std::uint8_t button,bool pressed){std::vector<std::uint8_t>p;p.reserve(18);put_u32_le(p,pressed?8u:9u);p.push_back(button);p.push_back(0);put_u32_be(p,0);put_u64_be(p,ts);return p;}
std::vector<std::uint8_t> build_mouse_move_payload(std::uint64_t ts,std::int16_t dx,std::int16_t dy){std::vector<std::uint8_t>p;p.reserve(22);put_u32_le(p,7);put_u16_be(p,(std::uint16_t)dx);put_u16_be(p,(std::uint16_t)dy);put_u16_be(p,0);put_u32_be(p,0);put_u64_be(p,ts);return p;}
std::vector<std::uint8_t> build_mouse_wheel_payload(std::uint64_t ts,std::int16_t delta){std::vector<std::uint8_t>p;p.reserve(22);put_u32_le(p,10);put_u16_be(p,0);put_u16_be(p,(std::uint16_t)delta);put_u16_be(p,0);put_u32_be(p,0);put_u64_be(p,ts);return p;}
std::vector<std::uint8_t> build_keyboard_payload(std::uint64_t ts,const KeyboardStroke&k,bool pressed){std::vector<std::uint8_t>p;p.reserve(18);put_u32_le(p,pressed?3u:4u);put_u16_be(p,k.keycode);put_u16_be(p,k.modifiers);put_u16_be(p,k.scancode);put_u64_be(p,ts);return p;}
std::vector<std::uint8_t> wrap_single_input(int ver,std::uint64_t ts,const std::vector<std::uint8_t>&p){if(ver<=2)return p;std::vector<std::uint8_t>o;o.reserve(10+p.size());o.push_back(0x23);put_u64_be(o,ts);o.push_back(0x22);o.insert(o.end(),p.begin(),p.end());return o;}
std::vector<std::uint8_t> wrap_reliable_gamepad(int ver,std::uint64_t ts,const std::vector<std::uint8_t>& p){if(ver<=2)return p;std::vector<std::uint8_t> o;o.reserve(12+p.size());o.push_back(0x23);put_u64_be(o,ts);o.push_back(0x21);put_u16_be(o,(std::uint16_t)p.size());o.insert(o.end(),p.begin(),p.end());return o;}
std::vector<std::uint8_t> wrap_partial_gamepad(int ver,std::uint64_t ts,std::uint8_t cid,std::uint16_t seq,const std::vector<std::uint8_t>& p){if(ver<=2)return p;std::vector<std::uint8_t> o;o.reserve(16+p.size());o.push_back(0x23);put_u64_be(o,ts);o.push_back(0x26);o.push_back(cid);put_u16_be(o,seq);o.push_back(0x21);put_u16_be(o,(std::uint16_t)p.size());o.insert(o.end(),p.begin(),p.end());return o;}
bool map_ascii_key(char c,KeyboardStroke&s){
 const std::uint16_t shift=1;static const std::uint8_t scans[26]={0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
 if(c>='a'&&c<='z'){s.keycode=(std::uint16_t)('A'+c-'a');s.scancode=scans[c-'a'];s.modifiers=0;return true;}if(c>='A'&&c<='Z'){s.keycode=(std::uint16_t)c;s.scancode=scans[c-'A'];s.modifiers=shift;return true;}
 if(c>='1'&&c<='9'){s.keycode=(std::uint16_t)c;s.scancode=(std::uint16_t)(2+c-'1');s.modifiers=0;return true;}if(c=='0'){s.keycode='0';s.scancode=0x0b;s.modifiers=0;return true;}
 switch(c){case ' ':s.keycode=0x20;s.scancode=0x39;s.modifiers=0;break;case '@':s.keycode='2';s.scancode=3;s.modifiers=shift;break;case '.':s.keycode=0xbe;s.scancode=0x34;s.modifiers=0;break;case ',':s.keycode=0xbc;s.scancode=0x33;s.modifiers=0;break;case '-':s.keycode=0xbd;s.scancode=0x0c;s.modifiers=0;break;case '_':s.keycode=0xbd;s.scancode=0x0c;s.modifiers=shift;break;case '=':s.keycode=0xbb;s.scancode=0x0d;s.modifiers=0;break;case '+':s.keycode=0xbb;s.scancode=0x0d;s.modifiers=shift;break;case '/':s.keycode=0xbf;s.scancode=0x35;s.modifiers=0;break;case '?':s.keycode=0xbf;s.scancode=0x35;s.modifiers=shift;break;case ':':s.keycode=0xba;s.scancode=0x27;s.modifiers=shift;break;case ';':s.keycode=0xba;s.scancode=0x27;s.modifiers=0;break;case '!':s.keycode='1';s.scancode=2;s.modifiers=shift;break;case '\n':case '\r':s.keycode=0x0d;s.scancode=0x1c;s.modifiers=0;break;case '\b':s.keycode=8;s.scancode=0x0e;s.modifiers=0;break;default:return false;}return true;
}
bool input_encoding_self_test(){GamepadState s;s.controller_id=2;s.controller_bitmap=0x0f0f;s.buttons=0x1234;s.left_trigger=0x56;s.right_trigger=0x78;s.lx=1.0f/32767.0f;s.ly=-2.0f/32767.0f;s.rx=3.0f/32767.0f;s.ry=-4.0f/32767.0f;auto raw=build_gamepad_payload(0x0102030405060708ULL,s);if(raw.size()!=38||raw[0]!=12||raw[6]!=2||raw[8]!=0x0f||raw[9]!=0x0f||raw[26]!=0x55||raw[30]!=0x08||raw[37]!=0x01)return false;auto r=wrap_reliable_gamepad(3,0x0102030405060708ULL,raw);auto q=wrap_partial_gamepad(3,0x0102030405060708ULL,2,1,raw);auto m=build_mouse_move_payload(0x0102030405060708ULL,10,-5);auto b=wrap_single_input(3,0x0102030405060708ULL,build_mouse_button_payload(0x0102030405060708ULL,1,true));KeyboardStroke k;if(!map_ascii_key('A',k))return false;auto key=build_keyboard_payload(0x0102030405060708ULL,k,true);return r.size()==50&&r[0]==0x23&&r[9]==0x21&&r[10]==0&&r[11]==38&&q.size()==54&&q[9]==0x26&&q[10]==2&&q[13]==0x21&&m.size()==22&&m[0]==7&&m[4]==0&&m[5]==10&&m[6]==0xff&&m[7]==0xfb&&b.size()==28&&b[9]==0x22&&b[10]==8&&key.size()==18&&key[0]==3&&key[4]==0&&key[5]=='A';}
}
