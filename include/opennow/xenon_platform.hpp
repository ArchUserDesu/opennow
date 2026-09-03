#pragma once
#include "input_protocol.hpp"
#include "video_frame.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
namespace opennow {
class XenonPlatform {
public:
    bool init();
    void poll();
    bool read_gamepad(GamepadState&);
    bool present(const VideoFrame&);
    bool play_pcm48_stereo(const std::int16_t*, std::size_t frames);
    void log(const char*);
    // Minimal text UI used by the XEX target. LibXenon maps these to stdout.
    void clear_text();
    void write_text(const char*);
    void set_stream_overlay(const char*);
};
}
