#pragma once
#include "models.hpp"
#include <string>
namespace opennow {
struct RiInputCaps {
    int threshold_ms;
    unsigned hid_mask;
    unsigned gamepad_mask;
    unsigned hid_partial_mask;
    RiInputCaps() : threshold_ms(16), hid_mask(0), gamepad_mask(1), hid_partial_mask(0) {}
};
RiInputCaps parse_ri_caps(const std::string& offer);
std::string prepare_gfn_offer(std::string offer,const std::string& signaling_url,const std::string& media_ip);
std::string adapt_gfn_answer(const std::string& answer,const std::string& offer,const StreamConfig& cfg);
std::string build_nvst_sdp(const std::string& answer,const StreamConfig& cfg,const RiInputCaps& caps);
std::string build_manual_candidate(const std::string& signaling_url,const std::string& media_ip,int port,int foundation=1);
}
