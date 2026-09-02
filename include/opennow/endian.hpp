#pragma once
#include <cstdint>
#include <vector>
namespace opennow {
inline void put_u16_le(std::vector<std::uint8_t>& o, std::uint16_t v){o.push_back(v&0xff);o.push_back((v>>8)&0xff);}
inline void put_i16_le(std::vector<std::uint8_t>& o, std::int16_t v){put_u16_le(o,(std::uint16_t)v);}
inline void put_u32_le(std::vector<std::uint8_t>& o, std::uint32_t v){for(int i=0;i<4;i++)o.push_back((v>>(8*i))&0xff);}
inline void put_u64_le(std::vector<std::uint8_t>& o, std::uint64_t v){for(int i=0;i<8;i++)o.push_back((v>>(8*i))&0xff);}
inline void put_u16_be(std::vector<std::uint8_t>& o, std::uint16_t v){o.push_back((v>>8)&0xff);o.push_back(v&0xff);}
inline void put_u32_be(std::vector<std::uint8_t>& o, std::uint32_t v){for(int i=3;i>=0;i--)o.push_back((v>>(8*i))&0xff);}
inline void put_u64_be(std::vector<std::uint8_t>& o, std::uint64_t v){for(int i=7;i>=0;i--)o.push_back((v>>(8*i))&0xff);}
inline std::uint16_t read_u16_be(const std::uint8_t* p){return (std::uint16_t(p[0])<<8)|p[1];}
inline std::uint32_t read_u32_be(const std::uint8_t* p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
}
