#pragma once
#include "video_frame.hpp"
#include <cstdint>
#include <vector>
namespace opennow { bool to_argb8888(const VideoFrame& in,std::vector<std::uint32_t>& out); }
