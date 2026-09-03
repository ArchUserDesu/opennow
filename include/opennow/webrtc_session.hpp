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
#if defined(OPENNOW_XDK)
#include <xtl.h>
#endif
extern "C" {
#include "peer_connection.h"
}
namespace opennow {
class WebRtcSession {
public:
    WebRtcSession(const SessionInfo&,const StreamConfig&,XenonPlatform&);
    ~WebRtcSession();
    bool start();
    bool poll();
    bool send_gamepad(const GamepadState&);
    bool send_mouse_move(std::int16_t,std::int16_t);
    bool send_mouse_button(std::uint8_t,bool);
    bool send_mouse_wheel(std::int16_t);
    bool send_key(const KeyboardStroke&,bool);
    bool send_text(const std::string&,bool);
    bool running()const{return running_;}
    bool has_video()const{return decoded_frames_>0;}
    std::string state()const{return state_;}
    int rtt_ms()const;
    void stop();
private:
    SessionInfo info_; StreamConfig cfg_; XenonPlatform& platform_; WebSocket ws_;
    PeerConnection* pc_; VideoDecoder* decoder_; AudioDecoder* audio_decoder_;
    std::string peer_name_; int peer_id_,remote_peer_id_,ack_;
    bool running_,signaling_open_,media_established_,remote_set_,answer_sent_,offer_seen_,sctp_open_,channel_requested_,input_ready_,manual_candidate_added_,waiting_video_keyframe_,audio_have_sequence_;
    int input_protocol_; uint16_t gamepad_seq_; std::string state_; std::vector<std::string> local_candidates_,pending_manual_candidates_;
    int remote_ice_count_,local_ice_count_;
    std::uint64_t last_hb_us_,last_peer_info_us_,last_input_hb_us_,channel_request_us_,input_activation_us_,manual_candidate_due_us_,last_pli_us_;
    std::uint16_t audio_last_sequence_;
    std::vector<std::vector<std::uint8_t> > pending_video_units_;
    std::uint64_t video_packets_,video_queue_drops_,video_present_drops_,audio_packets_,audio_missing_packets_,audio_recovered_packets_,audio_concealed_packets_,audio_resyncs_,audio_input_queue_drops_,audio_starvations_,decoded_frames_,video_decode_failures_,audio_decode_failures_,audio_output_failures_,input_packets_,mouse_packets_,key_packets_;
#if defined(OPENNOW_XDK)
    struct QueuedAudioPacket {
        std::vector<std::uint8_t> data;
        std::uint16_t sequence;
        int payload_type;
        QueuedAudioPacket():sequence(0),payload_type(0){}
    };
    HANDLE network_thread_,video_thread_,audio_thread_,video_event_,audio_event_;
    volatile LONG workers_stop_,keyframe_pending_;
    mutable CRITICAL_SECTION peer_cs_;
    CRITICAL_SECTION video_cs_,frame_cs_,audio_cs_,candidate_cs_;
    std::vector<QueuedAudioPacket> pending_audio_packets_;
    VideoFrame ready_video_frame_;
    bool ready_video_available_;
    static DWORD WINAPI network_thread_entry(LPVOID);
    static DWORD WINAPI video_thread_entry(LPVOID);
    static DWORD WINAPI audio_thread_entry(LPVOID);
    bool start_workers();
    void stop_workers();
    void network_loop();
    void video_loop();
    void audio_loop();
#endif
    void lock_peer()const;
    void unlock_peer()const;
    void handle_signal(const std::string&);
    void send_peer_payload(void* json);
    void send_peer_info();
    void heartbeat();
    void maybe_keepalive();
    void maybe_open_input();
    void schedule_manual_candidates(const std::string&,const std::string&);
    void maybe_manual_candidate();
    void flush_local_candidates();
    void request_keyframe_async();
    void decode_pending_video();
    void present_ready_video();
    bool submit_opus(const std::uint8_t*,std::size_t,bool);
    bool submit_plc();
    void process_audio_packet(const std::uint8_t*,std::size_t,std::uint16_t,int);
    void on_ice(const std::string&);
    void on_state(int);
    void on_data(char*,size_t,uint16_t);
    void on_open();
    void on_close();
    void on_video(const PeerVideoPacket&);
    void on_audio(const PeerAudioPacket&);
    static void ice_cb(char*,void*);
    static void state_cb(PeerConnectionState,void*);
    static void data_cb(char*,size_t,void*,uint16_t);
    static void open_cb(void*);
    static void close_cb(void*);
    static void video_cb(const PeerVideoPacket*,void*);
    static void audio_cb(const PeerAudioPacket*,void*);
};
}
