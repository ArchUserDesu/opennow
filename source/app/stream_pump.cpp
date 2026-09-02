#include "opennow/stream_pump.hpp"
namespace opennow {bool StreamPump::step(){EncodedAccessUnit u;if(!q_.pop(u))return false;VideoFrame f;if(!decoder_.decode(u.bytes.empty()?NULL:&u.bytes[0],u.bytes.size(),f))return false;return platform_.present(f);}}
