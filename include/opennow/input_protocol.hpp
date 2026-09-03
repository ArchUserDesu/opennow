#pragma once
#include <cstdint>
#include <vector>
namespace opennow {
struct GamepadState {
  std::uint8_t controller_id; std::uint16_t controller_bitmap; std::uint16_t buttons;
  std::uint8_t left_trigger,right_trigger; float lx,ly,rx,ry;
  GamepadState():controller_id(0),controller_bitmap(1),buttons(0),left_trigger(0),right_trigger(0),lx(0),ly(0),rx(0),ry(0){}
};
struct KeyboardStroke {
  std::uint16_t keycode,scancode,modifiers;
  KeyboardStroke():keycode(0),scancode(0),modifiers(0){}
};
std::vector<std::uint8_t> build_gamepad_payload(std::uint64_t timestamp_us,const GamepadState& s);
std::vector<std::uint8_t> build_mouse_button_payload(std::uint64_t timestamp_us,std::uint8_t button,bool pressed);
std::vector<std::uint8_t> build_mouse_move_payload(std::uint64_t timestamp_us,std::int16_t dx,std::int16_t dy);
std::vector<std::uint8_t> build_mouse_wheel_payload(std::uint64_t timestamp_us,std::int16_t delta);
std::vector<std::uint8_t> build_keyboard_payload(std::uint64_t timestamp_us,const KeyboardStroke& key,bool pressed);
std::vector<std::uint8_t> wrap_single_input(int protocol_version,std::uint64_t timestamp_us,const std::vector<std::uint8_t>& payload);
std::vector<std::uint8_t> wrap_reliable_gamepad(int protocol_version,std::uint64_t timestamp_us,const std::vector<std::uint8_t>& payload);
std::vector<std::uint8_t> wrap_partial_gamepad(int protocol_version,std::uint64_t timestamp_us,std::uint8_t controller_id,std::uint16_t sequence,const std::vector<std::uint8_t>& payload);
bool map_ascii_key(char character,KeyboardStroke& stroke);
bool input_encoding_self_test();
}
