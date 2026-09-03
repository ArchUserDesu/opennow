#!/usr/bin/env python3
import argparse
import re
from pathlib import Path

def read(path):
    return path.read_text(encoding='utf-8')

def write(path, text):
    path.write_text(text, encoding='utf-8')

def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError('%s: expected exactly one match, found %d' % (label, count))
    return text.replace(old, new, 1)

def regex_once(text, pattern, repl, label):
    out, count = re.subn(pattern, repl, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError('%s: expected exactly one regex match, found %d' % (label, count))
    return out

def patch_audio_header(root):
    p = root / 'include/opennow/audio_decoder.hpp'
    t = read(p)
    old = 'virtual bool decode(const std::uint8_t* packet,std::size_t bytes,std::vector<std::int16_t>& pcm,int& frames)=0; virtual void reset()=0;'
    new = 'virtual bool decode(const std::uint8_t* packet,std::size_t bytes,std::vector<std::int16_t>& pcm,int& frames)=0; virtual bool conceal(int frame_size,std::vector<std::int16_t>& pcm,int& frames)=0; virtual void reset()=0;'
    t = replace_once(t, old, new, 'audio decoder PLC interface')
    write(p, t)

def patch_opus(root):
    p = root / 'source/media/opus_audio_decoder.cpp'
    t = read(p)
    needle = '    void reset(){if(dec_)opus_decoder_ctl(dec_,OPUS_RESET_STATE);}'
    insert = '''    bool conceal(int frame_size,std::vector<std::int16_t>& pcm,int& frames){frames=0;if(!dec_||frame_size<=0)return false;pcm.resize((std::size_t)frame_size*(std::size_t)channels_);int n=opus_decode(dec_,NULL,0,&pcm[0],frame_size,0);if(n<0){ON_LOGE("audio-decode","opus PLC failed err=%d text=%s frame_size=%d",n,opus_strerror(n),frame_size);pcm.clear();return false;}frames=n;pcm.resize((std::size_t)n*(std::size_t)channels_);return true;}\n    void reset(){if(dec_)opus_decoder_ctl(dec_,OPUS_RESET_STATE);}'''
    t = replace_once(t, needle, insert, 'Opus PLC')
    write(p, t)

def patch_webrtc_header(root):
    p = root / 'include/opennow/webrtc_session.hpp'
    t = read(p)
    t = replace_once(t, '#include <vector>\n', '#include <vector>\n#if defined(OPENNOW_XDK)\n#include <xtl.h>\n#endif\n', 'XDK thread declarations include')
    old = 'std::uint64_t video_packets_,video_queue_drops_,audio_packets_,audio_missing_packets_,audio_recovered_packets_,decoded_frames_,video_decode_failures_,audio_decode_failures_,audio_output_failures_,input_packets_,mouse_packets_,key_packets_;'
    new = 'std::uint64_t video_packets_,video_queue_drops_,audio_packets_,audio_missing_packets_,audio_recovered_packets_,audio_concealed_packets_,decoded_frames_,video_decode_failures_,audio_decode_failures_,audio_output_failures_,input_packets_,mouse_packets_,key_packets_;'
    t = replace_once(t, old, new, 'audio conceal counter')
    old = 'void decode_pending_video(); bool submit_opus(const std::uint8_t*,std::size_t,bool); void on_ice'
    new = 'void decode_pending_video(); bool submit_opus(const std::uint8_t*,std::size_t,bool); bool submit_plc(unsigned); void on_ice'
    t = replace_once(t, old, new, 'PLC method declaration')
    marker = '    static void ice_cb(char*,void*);'
    insert = '''#if defined(OPENNOW_XDK)\n    HANDLE network_thread_; volatile LONG network_running_; mutable CRITICAL_SECTION peer_lock_; bool peer_lock_ready_;\n    static DWORD WINAPI network_thread_proc(LPVOID); void start_network_worker(); void stop_network_worker(); void network_loop();\n#endif\n    void lock_peer() const; void unlock_peer() const; int add_ice_locked(char*); PeerConnectionState peer_state_locked() const; void request_keyframe_locked(); int send_binary_locked(char*,size_t,uint16_t); int create_datachannel_locked(char*,char*);\n    static void ice_cb(char*,void*);'''
    t = replace_once(t, marker, insert, 'transport worker declarations')
    write(p, t)

def patch_webrtc_cpp(root):
    p = root / 'source/webrtc/webrtc_session.cpp'
    t = read(p)
    ctor_pat = r'WebRtcSession::WebRtcSession\(const SessionInfo&i,const StreamConfig&c,XenonPlatform&p\)\n:.*?\{\}\nWebRtcSession::~WebRtcSession\(\)\{stop\(\);\}'
    ctor_repl = '''WebRtcSession::WebRtcSession(const SessionInfo&i,const StreamConfig&c,XenonPlatform&p)\n:info_(i),cfg_(c),platform_(p),pc_(NULL),decoder_(NULL),audio_decoder_(NULL),peer_name_(make_peer()),peer_id_(0),remote_peer_id_(1),ack_(0),running_(false),signaling_open_(false),media_established_(false),remote_set_(false),answer_sent_(false),offer_seen_(false),sctp_open_(false),channel_requested_(false),input_ready_(false),manual_candidate_added_(false),waiting_video_keyframe_(false),audio_have_sequence_(false),input_protocol_(2),gamepad_seq_(1),state_("idle"),remote_ice_count_(0),local_ice_count_(0),last_hb_us_(0),last_peer_info_us_(0),last_input_hb_us_(0),channel_request_us_(0),input_activation_us_(0),manual_candidate_due_us_(0),last_pli_us_(0),audio_last_sequence_(0),video_packets_(0),video_queue_drops_(0),audio_packets_(0),audio_missing_packets_(0),audio_recovered_packets_(0),audio_concealed_packets_(0),decoded_frames_(0),video_decode_failures_(0),audio_decode_failures_(0),audio_output_failures_(0),input_packets_(0),mouse_packets_(0),key_packets_(0)\n#if defined(OPENNOW_XDK)\n,network_thread_(NULL),network_running_(0),peer_lock_ready_(false)\n#endif\n{\n#if defined(OPENNOW_XDK)\n    InitializeCriticalSection(&peer_lock_); peer_lock_ready_=true;\n#endif\n}\nWebRtcSession::~WebRtcSession(){stop();\n#if defined(OPENNOW_XDK)\n    if(peer_lock_ready_){DeleteCriticalSection(&peer_lock_);peer_lock_ready_=false;}\n#endif\n}'''
    t = regex_once(t, ctor_pat, ctor_repl, 'WebRTC constructor/thread lock')

    t = t.replace('peer_connection_add_ice_candidate(pc_,const_cast<char*>(c.c_str()))', 'add_ice_locked(const_cast<char*>(c.c_str()))')
    t = t.replace('peer_connection_get_state(pc_)', 'peer_state_locked()')
    t = t.replace('peer_connection_request_video_keyframe(pc_)', 'request_keyframe_locked()')
    t = t.replace('peer_connection_datachannel_send_binary_sid(pc_,', 'send_binary_locked(')
    t = t.replace('peer_connection_create_datachannel_sid(pc_,DATA_CHANNEL_RELIABLE,0,0,label,protocol,0)', 'create_datachannel_locked(label,protocol)')

    stop_marker = 'void WebRtcSession::stop(){'
    worker_code = '''void WebRtcSession::lock_peer() const {\n#if defined(OPENNOW_XDK)\n    if(peer_lock_ready_) EnterCriticalSection(&peer_lock_);\n#endif\n}\nvoid WebRtcSession::unlock_peer() const {\n#if defined(OPENNOW_XDK)\n    if(peer_lock_ready_) LeaveCriticalSection(&peer_lock_);\n#endif\n}\nint WebRtcSession::add_ice_locked(char*c){lock_peer();int rc=pc_?peer_connection_add_ice_candidate(pc_,c):-1;unlock_peer();return rc;}\nPeerConnectionState WebRtcSession::peer_state_locked() const {lock_peer();PeerConnectionState s=pc_?peer_connection_get_state(pc_):PEER_CONNECTION_NEW;unlock_peer();return s;}\nvoid WebRtcSession::request_keyframe_locked(){lock_peer();if(pc_)peer_connection_request_video_keyframe(pc_);unlock_peer();}\nint WebRtcSession::send_binary_locked(char*d,size_t n,uint16_t sid){lock_peer();int rc=pc_?peer_connection_datachannel_send_binary_sid(pc_,d,n,sid):-1;unlock_peer();return rc;}\nint WebRtcSession::create_datachannel_locked(char*label,char*protocol){lock_peer();int rc=pc_?peer_connection_create_datachannel_sid(pc_,DATA_CHANNEL_RELIABLE,0,0,label,protocol,0):-1;unlock_peer();return rc;}\n\n#if defined(OPENNOW_XDK)\nDWORD WINAPI WebRtcSession::network_thread_proc(LPVOID arg){WebRtcSession*s=(WebRtcSession*)arg;if(s)s-network_loop();return 0;}\nvoid WebRtcSession::start_network_worker(){\n    if(network_thread_||!pc_||!remote_set_)return;\n    InterlockedExchange(&network_running_,1);\n    network_thread_=CreateThread(NULL,0,&WebRtcSession::network_thread_proc,this,0,NULL);\n    if(!network_thread_){InterlockedExchange(&network_running_,0);ON_LOGE("transport","failed to create XDK network worker; falling back to cooperative polling");return;}\n    SetThreadPriority(network_thread_,THREAD_PRIORITY_HIGHEST);\n    ON_LOGI("transport","XDK network worker started priority=highest batch_datagrams=32 batch_ms=1");\n}\nvoid WebRtcSession::stop_network_worker(){\n    InterlockedExchange(&network_running_,0);\n    if(network_thread_){WaitForSingleObject(network_thread_,INFINITE);CloseHandle(network_thread_);network_thread_=NULL;}\n}\nvoid WebRtcSession::network_loop(){\n    while(InterlockedCompareExchange(&network_running_,1,1)==1){\n        int batch=0; bool active=false;\n        lock_peer();\n        if(running_&&pc_&&remote_set_){\n            active=true;DWORD began=GetTickCount();\n            for(int i=0;i<32;++i){if(peer_connection_loop(pc_)==0)break;++batch;if(batch>=8&&GetTickCount()!=began)break;}\n        }\n        unlock_peer();\n        if(batch==0)Sleep(active?1:2);else Sleep(0);\n    }\n    ON_LOGI("transport","XDK network worker stopped");\n}\n#endif\n\n'''
    t = replace_once(t, stop_marker, worker_code + stop_marker, 'transport worker implementation')

    stop_pat = r'void WebRtcSession::stop\(\)\{.*?\}\nint WebRtcSession::rtt_ms\(\)const\{return pc_\?peer_connection_get_rtt_ms\(pc_\):-1;\}'
    stop_repl = '''void WebRtcSession::stop(){if(!pc_&&!running_&&!decoder_&&!audio_decoder_)return;ON_LOGI("webrtc","stop begin state=%s video_units=%llu decoded=%llu video_failures=%llu video_queue_drops=%llu audio_packets=%llu audio_missing=%llu audio_recovered=%llu audio_concealed=%llu audio_decode_failures=%llu audio_output_failures=%llu gamepad=%llu mouse=%llu keys=%llu", state_.c_str(),video_packets_,decoded_frames_,video_decode_failures_,video_queue_drops_,audio_packets_,audio_missing_packets_,audio_recovered_packets_,audio_concealed_packets_,audio_decode_failures_,audio_output_failures_,input_packets_,mouse_packets_,key_packets_);running_=false;signaling_open_=false;ws_.close();\n#if defined(OPENNOW_XDK)\nstop_network_worker();\n#endif\nif(pc_){lock_peer();peer_connection_close(pc_);peer_connection_destroy(pc_);pc_=NULL;unlock_peer();peer_deinit();}lock_peer();pending_video_units_.clear();unlock_peer();if(decoder_)decoder_->flush();delete decoder_;decoder_=NULL;if(audio_decoder_)audio_decoder_->reset();delete audio_decoder_;audio_decoder_=NULL;state_="stopped";ON_LOGI("webrtc","stop complete");}\nint WebRtcSession::rtt_ms()const{lock_peer();int r=pc_?peer_connection_get_rtt_ms(pc_):-1;unlock_peer();return r;}'''
    t = regex_once(t, stop_pat, stop_repl, 'stop worker before peer destruction')

    poll_pat = r'bool WebRtcSession::poll\(\)\{.*?return running_;\}'
    poll_repl = '''bool WebRtcSession::poll(){if(!running_)return false;std::vector<std::string> ms;if(signaling_open_&&!ws_.poll(ms)){PeerConnectionState ps=peer_state_locked();if(media_established_&&(ps==PEER_CONNECTION_CONNECTED||ps==PEER_CONNECTION_COMPLETED)){ON_LOGW("signaling","channel closed after media established; continuing ICE/DTLS transport error=%s state=%s",ws_.error().c_str(),peer_connection_state_to_string(ps));signaling_open_=false;ws_.close();state_="media connected";}else{state_="signaling disconnected: "+ws_.error();ON_LOGE("signaling","poll failed before media established error=%s peer_state=%s",ws_.error().c_str(),pc_?peer_connection_state_to_string(ps):"none");running_=false;return false;}}if(!ms.empty()ON_LOGD("signaling","received messages=%u",(unsigned)ms.size());for(size_t i=0;i<ms.size();++i)handle_signal(ms[i]);if(signaling_open_)maybe_keepalive();\n#if defined(OPENNOW_XDK)\nif(pc_&&remote_set_&&!network_thread_)start_network_worker();\nif(pc_&&remote_set_&&!network_thread_){lock_peer();for(int i=0;i<256;++i)if(peer_connection_loop(pc_)==0)break;unlock_peer();}\n#else\nif(pc_&&remote_set_){for(int i=0;i<256;++i)if(peer_connection_loop(pc_)==0)break;}\n#endif\ndecode_pending_video();maybe_manual_candidate();maybe_open_input();return running_;}'''
    t = regex_once(t, poll_pat, poll_repl, 'non-starving poll loop')

    decode_pat = r'void WebRtcSession::decode_pending_video\(\)\{.*?\n\}'
    decode_repl = '''MÄ