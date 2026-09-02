#pragma once
#include <cstdint>
#include <vector>
namespace opennow {
struct GamepadState {
  std::uint8_t controller_id; std::uint16_t controller_bitmap; std::uint16_t buttons;
  std::uint8_t left_trigger,right_trigger; float lx,ly,rx,ry;
  GamepadState():controller_id(0),controller_bitmap(1),buttons(0),left_trigger(0),right_trigger(0),lx(0),ly(0),rx(0),ry(0){}
};
std::vector<std::uint8_t> build_gamepad_payload(std::uint64_t timestamp_us,const GamepadState& s);
std::vector<std::uint8_t> wrap_reliable_gamepad(int protocol_version,std::uint64_t timestamp_us,const std::vector<std::uint8_t>& payload);
std::vector<std::uint8_t> wrap_partial_gamepad(int protocol_version,std::uint64_t timestamp_us,std::uint8_t controller_id,std::uint16_t sequence,const std::vector<std::uint8_t>& payload);
bool input_encoding_self_test();
}
