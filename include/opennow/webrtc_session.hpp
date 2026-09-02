#pragma once
#include "models.hpp"
#include "audio_decoder.hpp"
#include "video_decoder.hpp"
#include "xenon_platform.hpp"
#include "input_protocol.hpp"
#include "websocket.hpp"
#include <cstdint>
#include <string>
#include <vector>
extern "C" {
#include "peer_connection.h"
}
namespace opennow {
class WebRtcSession {
public:
    WebRtcSession(const SessionInfo&,const StreamConfig&,XenonPlatform&);
    ~WebRtcSession();
    bool start(); bool poll(); bool send_gamepad(const GamepadState&); bool running()const{return running_;} std::string state()const{return state_;} int rtt_ms()const; void stop();
private:
    SessionInfo info_; StreamConfig cfg_; XenonPlatform& platform_; WebSocket ws_;
    PeerConnection* pc_; VideoDecoder* decoder_; AudioDecoder* audio_decoder_;
    std::string peer_name_; int peer_id_,remote_peer_id_,ack_;
    bool running_,remote_set_,answer_sent_,offer_seen_,sctp_open_,channel_requested_,input_ready_,manual_candidate_added_;
    int input_protocol_; uint16_t gamepad_seq_; std::string state_; std::vector<std::string> local_candidates_;
    std::uint64_t last_hb_us_,last_peer_info_us_,last_input_hb_us_,channel_request_us_,input_activation_us_,manual_candidate_due_us_,last_pli_us_;
    std::uint64_t video_packets_,audio_packets_,decoded_frames_,video_decode_failures_,audio_decode_failures_,input_packets_;
    void handle_signal(const std::string&); void send_peer_payload(void* json); void send_peer_info(); void heartbeat(); void maybe_keepalive(); void maybe_open_input(); void maybe_manual_candidate();
    void on_ice(const std::string&); void on_state(int); void on_data(char*,size_t,uint16_t); void on_open(); void on_close(); void on_video(const PeerVideoPacket&); void on_audio(const PeerAudioPacket&);
    static void ice_cb(char*,void*); static void state_cb(PeerConnectionState,void*); static void data_cb(char*,size_t,void*,uint16_t); static void open_cb(void*); static void close_cb(void*); static void video_cb(const PeerVideoPacket*,void*); static void audio_cb(const PeerAudioPacket*,void*);
};
}
