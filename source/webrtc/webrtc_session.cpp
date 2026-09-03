#include "opennow/webrtc_session.hpp"
#include "opennow/gfn_sdp.hpp"
#include "opennow/json_util.hpp"
#include "opennow/logger.hpp"
extern "C" {
#include "peer.h"
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(OPENNOW_XDK)
#include <xtl.h>
#else
#include <chrono>
#endif

namespace opennow {
namespace {
uint64_t now_us(){
#if defined(OPENNOW_XDK)
    return (uint64_t)GetTickCount()*1000ULL;
#else
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
std::string int_text(long long v){char b[48];std::sprintf(b,"%lld",v);return b;}
std::string make_peer(){char b[40];std::sprintf(b,"peer-%llu",(unsigned long long)(now_us()%10000000000ULL));return b;}
std::string signin_url(std::string u,const std::string&peer,const std::string&sid){std::string::size_type q=u.find('?');if(q!=std::string::npos)u.resize(q);if(u.compare(0,8,"https://")==0)u.replace(0,5,"wss");else if(u.compare(0,7,"http://")==0)u.replace(0,4,"ws");while(!u.empty()&&u[u.size()-1]=='/')u.erase(u.size()-1);if(u.size()<8||u.substr(u.size()-8)!="sign_in")u+="/sign_in";return u+"?peer_id="+peer+"&version=2&peer_role=1&pairing_id="+sid;}
std::vector<std::string> sdp_lines(const std::string&s){std::vector<std::string>o;size_t p=0;while(p<s.size()){size_t e=s.find('\n',p);std::string l=s.substr(p,e==std::string::npos?std::string::npos:e-p);if(!l.empty()&&l[l.size()-1]=='\r')l.erase(l.size()-1);o.push_back(l);if(e==std::string::npos)break;p=e+1;}return o;}
std::string candidate_only(std::string s){if(s.compare(0,12,"a=candidate:")==0)s.erase(0,2);while(!s.empty()&&(s[s.size()-1]=='\r'||s[s.size()-1]=='\n'))s.erase(s.size()-1);return s;}
int sdp_media_port(const std::string&s,const char*kind){std::string p=std::string("m=")+kind+" ";std::vector<std::string>ls=sdp_lines(s);for(size_t i=0;i<ls.size();++i)if(ls[i].compare(0,p.size(),p)==0){const char*b=ls[i].c_str()+p.size();char*e=NULL;long n=std::strtol(b,&e,10);if(e!=b&&n>0&&n<=65535)return(int)n;}return 0;}
int sdp_attr_port(const std::string&s,const char*prefix){std::vector<std::string>ls=sdp_lines(s);size_t n=std::strlen(prefix);for(size_t i=0;i<ls.size();++i)if(ls[i].compare(0,n,prefix)==0){const char*b=ls[i].c_str()+n;char*e=NULL;long v=std::strtol(b,&e,10);if(e!=b&&v>0&&v<=65535)return(int)v;}return 0;}
void add_unique_port(std::vector<int>&ports,int port){if(port<=0||port>65535)return;for(size_t i=0;i<ports.size();++i)if(ports[i]==port)return;ports.push_back(port);}
bool contains_idr(const uint8_t*d,size_t n){if(!d)return false;for(size_t i=0;i+4<n;i++){size_t h=0;if(d[i]==0&&d[i+1]==0&&d[i+2]==1)h=i+3;else if(i+4<n&&d[i]==0&&d[i+1]==0&&d[i+2]==0&&d[i+3]==1)h=i+4;if(h&&h<n&&(d[h]&31)==5)return true;}return false;}
void move_frame(VideoFrame&dst,VideoFrame&src){dst.width=src.width;dst.height=src.height;dst.format=src.format;dst.pts=src.pts;for(int i=0;i<3;++i){dst.stride[i]=src.stride[i];dst.plane[i].swap(src.plane[i]);}}
}

WebRtcSession::WebRtcSession(const SessionInfo&i,const StreamConfig&c,XenonPlatform&p)
:info_(i),cfg_(c),platform_(p),pc_(NULL),decoder_(NULL),audio_decoder_(NULL),peer_name_(make_peer()),peer_id_(0),remote_peer_id_(1),ack_(0),running_(false),signaling_open_(false),media_established_(false),remote_set_(false),answer_sent_(false),offer_seen_(false),sctp_open_(false),channel_requested_(false),input_ready_(false),manual_candidate_added_(false),waiting_video_keyframe_(false),audio_have_sequence_(false),input_protocol_(2),gamepad_seq_(1),state_("idle"),remote_ice_count_(0),local_ice_count_(0),last_hb_us_(0),last_peer_info_us_(0),last_input_hb_us_(0),channel_request_us_(0),input_activation_us_(0),manual_candidate_due_us_(0),last_pli_us_(0),audio_last_sequence_(0),video_packets_(0),video_queue_drops_(0),video_present_drops_(0),audio_packets_(0),audio_missing_packets_(0),audio_recovered_packets_(0),audio_concealed_packets_(0),audio_resyncs_(0),audio_input_queue_drops_(0),audio_starvations_(0),decoded_frames_(0),video_decode_failures_(0),audio_decode_failures_(0),audio_output_failures_(0),input_packets_(0),mouse_packets_(0),key_packets_(0)
#if defined(OPENNOW_XDK)
,network_thread_(NULL),video_thread_(NULL),audio_thread_(NULL),video_event_(NULL),audio_event_(NULL),workers_stop_(1),keyframe_pending_(0),ready_video_available_(false)
#endif
{
#if defined(OPENNOW_XDK)
    InitializeCriticalSection(&peer_cs_);InitializeCriticalSection(&video_cs_);InitializeCriticalSection(&frame_cs_);InitializeCriticalSection(&audio_cs_);InitializeCriticalSection(&candidate_cs_);
#endif
}
WebRtcSession::~WebRtcSession(){stop();
#if defined(OPENNOW_XDK)
DeleteCriticalSection(&candidate_cs_);DeleteCriticalSection(&audio_cs_);DeleteCriticalSection(&frame_cs_);DeleteCriticalSection(&video_cs_);DeleteCriticalSection(&peer_cs_);
#endif
}

void WebRtcSession::lock_peer()const{
#if defined(OPENNOW_XDK)
    EnterCriticalSection(&peer_cs_);
#endif
}
void WebRtcSession::unlock_peer()const{
#if defined(OPENNOW_XDK)
    LeaveCriticalSection(&peer_cs_);
#endif
}

#if defined(OPENNOW_XDK)
DWORD WINAPI WebRtcSession::network_thread_entry(LPVOID p){WebRtcSession*s=(WebRtcSession*)p;if(s)s->network_loop();return 0;}
DWORD WINAPI WebRtcSession::video_thread_entry(LPVOID p){WebRtcSession*s=(WebRtcSession*)p;if(s)s->video_loop();return 0;}
DWORD WINAPI WebRtcSession::audio_thread_entry(LPVOID p){WebRtcSession*s=(WebRtcSession*)p;if(s)s->audio_loop();return 0;}
bool WebRtcSession::start_workers(){
    workers_stop_=0;keyframe_pending_=0;
    video_event_=CreateEvent(NULL,FALSE,FALSE,NULL);audio_event_=CreateEvent(NULL,FALSE,FALSE,NULL);
    if(!video_event_||!audio_event_){ON_LOGE("webrtc-worker","event creation failed");stop_workers();return false;}
    network_thread_=CreateThread(NULL,64*1024,&network_thread_entry,this,0,NULL);
    video_thread_=CreateThread(NULL,256*1024,&video_thread_entry,this,0,NULL);
    audio_thread_=CreateThread(NULL,96*1024,&audio_thread_entry,this,0,NULL);
    if(!network_thread_||!video_thread_||!audio_thread_){ON_LOGE("webrtc-worker","thread creation failed network=%p video=%p audio=%p",network_thread_,video_thread_,audio_thread_);stop_workers();return false;}
    SetThreadPriority(network_thread_,THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(audio_thread_,THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(video_thread_,THREAD_PRIORITY_ABOVE_NORMAL);
    ON_LOGI("webrtc-worker","workers started transport=highest audio=highest video=above-normal audio_jitter_packets=3 video_queue_max=3");
    return true;
}
void WebRtcSession::stop_workers(){
    InterlockedExchange(&workers_stop_,1);
    if(video_event_)SetEvent(video_event_);if(audio_event_)SetEvent(audio_event_);
    HANDLE hs[3];DWORD n=0;if(network_thread_)hs[n++]=network_thread_;if(video_thread_)hs[n++]=video_thread_;if(audio_thread_)hs[n++]=audio_thread_;
    if(n)WaitForMultipleObjects(n,hs,TRUE,3000);
    if(network_thread_){CloseHandle(network_thread_);network_thread_=NULL;}if(video_thread_){CloseHandle(video_thread_);video_thread_=NULL;}if(audio_thread_){CloseHandle(audio_thread_);audio_thread_=NULL;}
    if(video_event_){CloseHandle(video_event_);video_event_=NULL;}if(audio_event_){CloseHandle(audio_event_);audio_event_=NULL;}
}
void WebRtcSession::network_loop(){
    while(InterlockedCompareExchange(&workers_stop_,0,0)==0){
        bool worked=false;
        if(remote_set_){
            const std::uint64_t began=now_us();
            lock_peer();
            if(pc_){
                for(int i=0;i<32;++i){int rc=peer_connection_loop(pc_);if(rc==0)break;worked=true;if(now_us()-began>=1000ULL)break;}
                if(InterlockedExchange(&keyframe_pending_,0)!=0)peer_connection_request_video_keyframe(pc_);
            }
            unlock_peer();
        }
        if(!worked)Sleep(1);else Sleep(0);
    }
}
void WebRtcSession::video_loop(){
    while(InterlockedCompareExchange(&workers_stop_,0,0)==0){
        std::vector<std::uint8_t>data;
        EnterCriticalSection(&video_cs_);if(!pending_video_units_.empty()){data.swap(pending_video_units_[0]);pending_video_units_.erase(pending_video_units_.begin());}LeaveCriticalSection(&video_cs_);
        if(data.empty()){WaitForSingleObject(video_event_,2);continue;}
        const std::uint64_t began=now_us();VideoFrame f;
        if(decoder_&&decoder_->decode(&data[0],data.size(),f)){
            const std::uint64_t done=now_us();++decoded_frames_;
            EnterCriticalSection(&frame_cs_);if(ready_video_available_)++video_present_drops_;move_frame(ready_video_frame_,f);ready_video_available_=true;LeaveCriticalSection(&frame_cs_);
            if(decoded_frames_==1||decoded_frames_%120==0)ON_LOGI("video-perf","decoded frame=%llu decode_ms=%llu compressed=%u present_drops=%llu",decoded_frames_,(done-began)/1000ULL,(unsigned)data.size(),video_present_drops_);
        }else{++video_decode_failures_;if(!contains_idr(&data[0],data.size()))request_keyframe_async();}
    }
}
void WebRtcSession::audio_loop(){
    bool primed=false;DWORD next_due=GetTickCount();
    while(InterlockedCompareExchange(&workers_stop_,0,0)==0){
        size_t queued=0;EnterCriticalSection(&audio_cs_);queued=pending_audio_packets_.size();LeaveCriticalSection(&audio_cs_);
        if(!primed){if(queued<3){WaitForSingleObject(audio_event_,2);continue;}primed=true;next_due=GetTickCount();}
        if(queued>12){
            unsigned dropped=0;EnterCriticalSection(&audio_cs_);if(pending_audio_packets_.size()>6){dropped=(unsigned)(pending_audio_packets_.size()-6);pending_audio_packets_.erase(pending_audio_packets_.begin(),pending_audio_packets_.end()-6);}LeaveCriticalSection(&audio_cs_);
            if(dropped){audio_input_queue_drops_+=dropped;audio_have_sequence_=false;if(audio_decoder_)audio_decoder_->reset();++audio_resyncs_;ON_LOGW("audio-jitter","trimmed stale compressed audio dropped=%u total=%llu resyncs=%llu",dropped,audio_input_queue_drops_,audio_resyncs_);}
        }
        DWORD now=GetTickCount();if((LONG)(next_due-now)>0){DWORD wait=next_due-now;WaitForSingleObject(audio_event_,wait>5?5:wait);continue;}
        QueuedAudioPacket q;bool have=false;EnterCriticalSection(&audio_cs_);if(!pending_audio_packets_.empty()){q=pending_audio_packets_[0];pending_audio_packets_.erase(pending_audio_packets_.begin());have=true;}LeaveCriticalSection(&audio_cs_);
        if(!have){primed=false;++audio_starvations_;if(audio_starvations_<=5||audio_starvations_%50==0)ON_LOGW("audio-jitter","input starvation count=%llu",audio_starvations_);continue;}
        process_audio_packet(q.data.empty()?NULL:&q.data[0],q.data.size(),q.sequence,q.payload_type);next_due+=10;now=GetTickCount();if((LONG)(now-next_due)>30)next_due=now+10;
    }
}
#endif

bool WebRtcSession::start(){
    ON_LOGI("webrtc","start begin target=%dx%d fps=%d ice_servers=%u",cfg_.width,cfg_.height,cfg_.fps,(unsigned)info_.ice_servers.size());
    decoder_=make_ffmpeg_h264_decoder();if(!decoder_||!decoder_->open(cfg_.width,cfg_.height,cfg_.fps)){state_="H.264 software decoder init failed";ON_LOGE("webrtc","%s",state_.c_str());delete decoder_;decoder_=NULL;return false;}
    audio_decoder_=make_opus_audio_decoder();if(!audio_decoder_||!audio_decoder_->open(48000,2)){state_="Opus software decoder init failed";ON_LOGE("webrtc","%s",state_.c_str());delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    int peer_rc=peer_init();if(peer_rc!=0){state_="libpeer initialization failed";ON_LOGE("webrtc","%s rc=%d",state_.c_str(),peer_rc);delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    PeerConfiguration c;std::memset(&c,0,sizeof(c));c.video_codec=CODEC_H264;c.audio_codec=CODEC_OPUS;c.datachannel=DATA_CHANNEL_STRING;c.onvideopacket=&video_cb;c.onaudiopacket=&audio_cb;c.user_data=this;
    if(info_.ice_servers.empty())info_.ice_servers.push_back(IceServerInfo("stun:s1.stun.gamestream.nvidia.com:19308","",""));const size_t n=std::min((size_t)5,info_.ice_servers.size());for(size_t k=0;k<n;k++){c.ice_servers[k].urls=info_.ice_servers[k].url.c_str();c.ice_servers[k].username=info_.ice_servers[k].username.empty()?NULL:info_.ice_servers[k].username.c_str();c.ice_servers[k].credential=info_.ice_servers[k].credential.empty()?NULL:info_.ice_servers[k].credential.c_str();}
    pc_=peer_connection_create(&c);if(!pc_){state_="peer_connection_create failed";ON_LOGE("webrtc","%s configured_ice=%u",state_.c_str(),(unsigned)n);peer_deinit();delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    ON_LOGI("webrtc","peer connection created pc=%p configured_ice=%u",pc_,(unsigned)n);peer_connection_onicecandidate(pc_,&ice_cb);peer_connection_oniceconnectionstatechange(pc_,&state_cb);peer_connection_ondatachannel(pc_,&data_cb,&open_cb,&close_cb);
    std::vector<std::string>h;h.push_back("Origin: https://play.geforcenow.com");h.push_back("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/131.0.0.0 Safari/537.36");h.push_back("Sec-WebSocket-Protocol: x-nv-sessionid."+info_.session_id);
    if(!ws_.connect(signin_url(info_.signaling_url,peer_name_,info_.session_id),h)){state_="signaling: "+ws_.error();ON_LOGE("signaling","connect failed error=%s",ws_.error().c_str());peer_connection_destroy(pc_);pc_=NULL;peer_deinit();delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    running_=true;signaling_open_=true;state_="signaling connected";
#if defined(OPENNOW_XDK)
    if(!start_workers()){running_=false;ws_.close();lock_peer();peer_connection_destroy(pc_);pc_=NULL;unlock_peer();peer_deinit();delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;state_="media worker startup failed";return false;}
#endif
    send_peer_info();heartbeat();return true;
}

void WebRtcSession::stop(){
    if(!pc_&&!running_&&!decoder_&&!audio_decoder_)return;
    ON_LOGI("webrtc","stop begin state=%s video_units=%llu decoded=%llu video_failures=%llu video_queue_drops=%llu video_present_drops=%llu audio_packets=%llu audio_missing=%llu audio_recovered=%llu audio_plc=%llu audio_resyncs=%llu audio_input_drops=%llu audio_starvations=%llu audio_decode_failures=%llu audio_output_failures=%llu gamepad=%llu mouse=%llu keys=%llu",state_.c_str(),video_packets_,decoded_frames_,video_decode_failures_,video_queue_drops_,video_present_drops_,audio_packets_,audio_missing_packets_,audio_recovered_packets_,audio_concealed_packets_,audio_resyncs_,audio_input_queue_drops_,audio_starvations_,audio_decode_failures_,audio_output_failures_,input_packets_,mouse_packets_,key_packets_);
    running_=false;signaling_open_=false;ws_.close();
#if defined(OPENNOW_XDK)
    stop_workers();
#endif
    lock_peer();if(pc_){peer_connection_close(pc_);peer_connection_destroy(pc_);pc_=NULL;}unlock_peer();peer_deinit();
#if defined(OPENNOW_XDK)
    EnterCriticalSection(&video_cs_);pending_video_units_.clear();LeaveCriticalSection(&video_cs_);EnterCriticalSection(&audio_cs_);pending_audio_packets_.clear();LeaveCriticalSection(&audio_cs_);EnterCriticalSection(&frame_cs_);ready_video_available_=false;for(int i=0;i<3;++i)ready_video_frame_.plane[i].clear();LeaveCriticalSection(&frame_cs_);
#else
    pending_video_units_.clear();
#endif
    if(decoder_)decoder_->flush();delete decoder_;decoder_=NULL;if(audio_decoder_)audio_decoder_->reset();delete audio_decoder_;audio_decoder_=NULL;state_="stopped";ON_LOGI("webrtc","stop complete");
}
int WebRtcSession::rtt_ms()const{int r=-1;lock_peer();if(pc_)r=peer_connection_get_rtt_ms(pc_);unlock_peer();return r;}

bool WebRtcSession::poll(){
    if(!running_)return false;std::vector<std::string>ms;
    if(signaling_open_&&!ws_.poll(ms)){PeerConnectionState ps=PEER_CONNECTION_NEW;lock_peer();if(pc_)ps=peer_connection_get_state(pc_);unlock_peer();if(media_established_&&(ps==PEER_CONNECTION_CONNECTED||ps==PEER_CONNECTION_COMPLETED)){ON_LOGW("signaling","channel closed after media established; continuing ICE/DTLS transport error=%s state=%s",ws_.error().c_str(),peer_connection_state_to_string(ps));signaling_open_=false;ws_.close();state_="media connected";}else{state_="signaling disconnected: "+ws_.error();ON_LOGE("signaling","poll failed before media established error=%s peer_state=%s",ws_.error().c_str(),pc_?peer_connection_state_to_string(ps):"none");running_=false;return false;}}
    if(!ms.empty())ON_LOGD("signaling","received messages=%u",(unsigned)ms.size());for(size_t i=0;i<ms.size();++i)handle_signal(ms[i]);if(signaling_open_)maybe_keepalive();flush_local_candidates();
#if !defined(OPENNOW_XDK)
    if(pc_&&remote_set_){for(int i=0;i<256;i++){if(peer_connection_loop(pc_)==0)break;if(pending_video_units_.size()>=2)break;}}decode_pending_video();
#else
    present_ready_video();
#endif
    maybe_manual_candidate();maybe_open_input();return running_;
}

void WebRtcSession::send_peer_info(){json_t*r=json_object(),*p=json_object();json_object_set_new(r,"ackid",json_integer(++ack_));json_object_set_new(p,"browser",json_string("Chrome"));json_object_set_new(p,"browserVersion",json_string("131"));json_object_set_new(p,"connected",json_true());json_object_set_new(p,"id",json_integer(peer_id_));json_object_set_new(p,"name",json_string(peer_name_.c_str()));json_object_set_new(p,"peerRole",json_integer(0));std::string res=int_text(cfg_.width)+"x"+int_text(cfg_.height);json_object_set_new(p,"resolution",json_string(res.c_str()));json_object_set_new(p,"version",json_integer(2));json_object_set_new(r,"peer_info",p);ws_.send_text(dump_json(r));json_decref(r);last_peer_info_us_=now_us();}
void WebRtcSession::heartbeat(){json_t*r=json_object();json_object_set_new(r,"hb",json_integer(1));ws_.send_text(dump_json(r));json_decref(r);last_hb_us_=now_us();}
void WebRtcSession::maybe_keepalive(){uint64_t n=now_us();if(!last_hb_us_||n-last_hb_us_>=5000000ULL)heartbeat();if(!offer_seen_&&(!last_peer_info_us_||n-last_peer_info_us_>=2000000ULL))send_peer_info();}
void WebRtcSession::send_peer_payload(void*vp){json_t*payload=(json_t*)vp;char*inner=json_dumps(payload,JSON_COMPACT);json_t*r=json_object(),*pm=json_object();json_object_set_new(pm,"from",json_integer(peer_id_));json_object_set_new(pm,"to",json_integer(remote_peer_id_));json_object_set_new(pm,"msg",json_string(inner?inner:"{}"));json_object_set_new(r,"peer_msg",pm);json_object_set_new(r,"ackid",json_integer(++ack_));ws_.send_text(dump_json(r));if(inner)free(inner);json_decref(r);}

void WebRtcSession::handle_signal(const std::string&m){
    JsonPtr root(NULL,&json_decref);try{root=parse_json(m);}catch(const std::exception&e){ON_LOGW("signaling","ignored invalid outer JSON bytes=%u error=%s",(unsigned)m.size(),e.what());return;}catch(...){return;}
    json_t*pi=json_object_get(root.get(),"peer_info");if(json_is_object(pi)&&js(pi,"name")==peer_name_)peer_id_=ji(pi,"id",peer_id_);json_t*aid=json_object_get(root.get(),"ackid");if(json_is_integer(aid)){bool should=true;if(json_is_object(pi)&&ji(pi,"id",-1)==peer_id_)should=false;if(should){json_t*a=json_object();json_object_set_new(a,"ack",json_integer(json_integer_value(aid)));ws_.send_text(dump_json(a));json_decref(a);}}if(json_object_get(root.get(),"hb")){heartbeat();return;}json_t*pm=json_object_get(root.get(),"peer_msg");if(!json_is_object(pm))return;remote_peer_id_=ji(pm,"from",remote_peer_id_);std::string raw=js(pm,"msg");if(raw.empty())return;JsonPtr p(NULL,&json_decref);try{p=parse_json(raw);}catch(...){return;}
    if(js(p.get(),"type")=="offer"&&pc_){offer_seen_=true;std::string offer=prepare_gfn_offer(js(p.get(),"sdp"),info_.signaling_url,info_.media_ip);RiInputCaps caps=parse_ri_caps(offer);input_protocol_=2;std::string answer;ON_LOGI("signaling","offer received bytes=%u remote_peer=%d",(unsigned)offer.size(),remote_peer_id_);lock_peer();if(pc_){peer_connection_set_remote_description(pc_,offer.c_str(),SDP_TYPE_OFFER);remote_set_=true;const char*ans=peer_connection_create_answer(pc_);if(ans)answer=ans;}unlock_peer();if(!answer.empty()){std::string adapted=adapt_gfn_answer(answer,offer,cfg_),nvst=build_nvst_sdp(adapted,cfg_,caps);ON_LOGI("signaling","answer created sdp_bytes=%u nvst_bytes=%u",(unsigned)adapted.size(),(unsigned)nvst.size());json_t*a=json_object();json_object_set_new(a,"type",json_string("answer"));json_object_set_new(a,"sdp",json_string(adapted.c_str()));json_object_set_new(a,"nvstSdp",json_string(nvst.c_str()));send_peer_payload(a);json_decref(a);answer_sent_=true;state_="answer sent";schedule_manual_candidates(offer,js(p.get(),"nvstSdp"));}else ON_LOGE("signaling","answer creation returned null");}
    else{std::string c=js(p.get(),"candidate");if(!c.empty()&&pc_){if(c.compare(0,12,"a=candidate:")!=0)c="a="+c;lock_peer();int rc=pc_?peer_connection_add_ice_candidate(pc_,const_cast<char*>(c.c_str())):-1;unlock_peer();ON_LOGI("ice","remote trickle candidate rc=%d bytes=%u",rc,(unsigned)c.size());if(rc==0){++remote_ice_count_;state_="remote ICE";}}}
}

void WebRtcSession::on_ice(const std::string&s){
#if defined(OPENNOW_XDK)
    EnterCriticalSection(&candidate_cs_);
#endif
    local_candidates_.push_back(s);unsigned q=(unsigned)local_candidates_.size();
#if defined(OPENNOW_XDK)
    LeaveCriticalSection(&candidate_cs_);
#endif
    if(q<=8||q%32==0)ON_LOGI("ice","local candidate queued callback_bytes=%u queue=%u",(unsigned)s.size(),q);
}
void WebRtcSession::flush_local_candidates(){
    if(!answer_sent_)return;std::vector<std::string>pending;
#if defined(OPENNOW_XDK)
    EnterCriticalSection(&candidate_cs_);
#endif
    pending.swap(local_candidates_);
#if defined(OPENNOW_XDK)
    LeaveCriticalSection(&candidate_cs_);
#endif
    for(size_t k=0;k<pending.size();++k){bool any=false;std::vector<std::string>lines=sdp_lines(pending[k]);for(size_t i=0;i<lines.size();++i){const std::string&l=lines[i];if(l.compare(0,12,"a=candidate:")!=0)continue;any=true;json_t*c=json_object();std::string v=candidate_only(l);json_object_set_new(c,"candidate",json_string(v.c_str()));json_object_set_new(c,"sdpMid",json_string("0"));json_object_set_new(c,"sdpMLineIndex",json_integer(0));send_peer_payload(c);json_decref(c);++local_ice_count_;}if(!any&&pending[k].compare(0,12,"a=candidate:")==0){json_t*c=json_object();std::string v=candidate_only(pending[k]);json_object_set_new(c,"candidate",json_string(v.c_str()));json_object_set_new(c,"sdpMid",json_string("0"));json_object_set_new(c,"sdpMLineIndex",json_integer(0));send_peer_payload(c);json_decref(c);++local_ice_count_;}}
}
void WebRtcSession::schedule_manual_candidates(const std::string&offer,const std::string&nvst){std::vector<int>ports;if(info_.media_port!=443)add_unique_port(ports,info_.media_port);add_unique_port(ports,sdp_media_port(offer,"video"));add_unique_port(ports,sdp_media_port(offer,"audio"));add_unique_port(ports,sdp_media_port(offer,"application"));add_unique_port(ports,sdp_attr_port(nvst,"a=general.serverBundlePort:"));pending_manual_candidates_.clear();for(size_t i=0;i<ports.size();++i){std::string c=build_manual_candidate(info_.signaling_url,info_.media_ip,ports[i],(int)i+1);if(!c.empty())pending_manual_candidates_.push_back(c);}manual_candidate_due_us_=pending_manual_candidates_.empty()?0:now_us()+900000ULL;std::string list;for(size_t i=0;i<ports.size();++i){if(i)list+=",";list+=int_text(ports[i]);}ON_LOGI("ice","manual fallback scheduled ports=%s candidates=%u media_hint_port=%d",list.c_str(),(unsigned)pending_manual_candidates_.size(),info_.media_port);}
void WebRtcSession::maybe_manual_candidate(){if(!pc_||manual_candidate_added_||pending_manual_candidates_.empty()||!manual_candidate_due_us_||now_us()<manual_candidate_due_us_)return;lock_peer();PeerConnectionState st=pc_?peer_connection_get_state(pc_):PEER_CONNECTION_CLOSED;if(st==PEER_CONNECTION_COMPLETED){unlock_peer();manual_candidate_added_=true;pending_manual_candidates_.clear();return;}int added=0;for(size_t i=0;i<pending_manual_candidates_.size();++i){std::string&c=pending_manual_candidates_[i];int rc=pc_?peer_connection_add_ice_candidate(pc_,const_cast<char*>(c.c_str())):-1;ON_LOGW("ice","manual fallback candidate index=%u rc=%d value=%s",(unsigned)i,rc,c.c_str());if(rc==0){++added;++remote_ice_count_;}}unlock_peer();pending_manual_candidates_.clear();manual_candidate_added_=true;state_=added?"fallback ICE added":"fallback ICE rejected";ON_LOGI("ice","manual fallback complete added=%d remote_total=%d",added,remote_ice_count_);}
void WebRtcSession::on_state(int st){if(st==PEER_CONNECTION_CONNECTED||st==PEER_CONNECTION_COMPLETED)media_established_=true;int total=0,frozen=0,checking=0,ok=0,failed=0;if(pc_)peer_connection_get_ice_candidate_pair_stats(pc_,&total,&frozen,&checking,&ok,&failed);ON_LOGI("webrtc-state","state changed code=%d name=%s rtt_ms=%d local_ice=%d remote_ice=%d pairs=%d frozen=%d checking=%d ok=%d failed=%d",st,peer_connection_state_to_string((PeerConnectionState)st),pc_?peer_connection_get_rtt_ms(pc_):-1,local_ice_count_,remote_ice_count_,total,frozen,checking,ok,failed);if(st==PEER_CONNECTION_FAILED||st==PEER_CONNECTION_CLOSED||st==PEER_CONNECTION_DISCONNECTED)running_=false;}
void WebRtcSession::maybe_open_input(){if(!pc_)return;lock_peer();if(!pc_||peer_connection_get_state(pc_)!=PEER_CONNECTION_COMPLETED){unlock_peer();return;}uint64_t n=now_us();if(!channel_requested_&&(!channel_request_us_||n-channel_request_us_>=750000ULL)){channel_request_us_=n;char label[]="input_channel_v1",protocol[]="";int r=peer_connection_create_datachannel_sid(pc_,DATA_CHANNEL_RELIABLE,0,0,label,protocol,0);if(r>=0){channel_requested_=true;input_activation_us_=n+1500000ULL;state_="input channel requested";}}if(channel_requested_&&sctp_open_&&!input_ready_&&input_activation_us_&&n>=input_activation_us_){input_protocol_=2;input_ready_=true;state_="input ready v2 fallback";}if(input_ready_&&(!last_input_hb_us_||n-last_input_hb_us_>=2000000ULL)){uint8_t hb[4]={2,0,0,0};peer_connection_datachannel_send_binary_sid(pc_,(char*)hb,sizeof(hb),0);last_input_hb_us_=n;}unlock_peer();}
void WebRtcSession::on_data(char*msg,size_t len,uint16_t sid){if(!msg||len<2)return;const uint8_t*b=(const uint8_t*)msg;uint16_t first=(uint16_t)(b[0]|(b[1]<<8));if(sid==0){int v=2;bool hs=false;if(first==526){v=len>=4?(int)(b[2]|(b[3]<<8)):2;hs=true;}else if(b[0]==0x0e){v=first;hs=true;}if(hs){input_protocol_=std::max(2,v);input_ready_=true;}}}
void WebRtcSession::on_open(){sctp_open_=true;ON_LOGI("datachannel","SCTP channel opened");}
void WebRtcSession::on_close(){ON_LOGW("datachannel","SCTP channel closed");sctp_open_=false;channel_requested_=false;input_ready_=false;channel_request_us_=now_us();}

void WebRtcSession::request_keyframe_async(){
#if defined(OPENNOW_XDK)
    InterlockedExchange(&keyframe_pending_,1);
#else
    if(pc_&&now_us()-last_pli_us_>500000ULL){last_pli_us_=now_us();peer_connection_request_video_keyframe(pc_);}
#endif
}
void WebRtcSession::on_video(const PeerVideoPacket&p){if(!decoder_||!p.data||!p.size)return;++video_packets_;const bool idr=contains_idr(p.data,p.size);
#if defined(OPENNOW_XDK)
    EnterCriticalSection(&video_cs_);
#endif
    if(waiting_video_keyframe_&&!idr){++video_queue_drops_;
#if defined(OPENNOW_XDK)
        LeaveCriticalSection(&video_cs_);
#endif
        return;}
    if(idr&&!pending_video_units_.empty()){video_queue_drops_+=pending_video_units_.size();pending_video_units_.clear();}
    if(pending_video_units_.size()>=3){video_queue_drops_+=pending_video_units_.size();pending_video_units_.clear();waiting_video_keyframe_=!idr;request_keyframe_async();static unsigned backlog_events=0;++backlog_events;if(backlog_events<=5||backlog_events%50==0)ON_LOGW("video-queue","bounded backlog event=%u dropped_total=%llu current_idr=%d waiting_for_idr=%d",backlog_events,video_queue_drops_,idr?1:0,waiting_video_keyframe_?1:0);if(waiting_video_keyframe_){++video_queue_drops_;
#if defined(OPENNOW_XDK)
        LeaveCriticalSection(&video_cs_);
#endif
        return;}}
    if(idr)waiting_video_keyframe_=false;pending_video_units_.push_back(std::vector<std::uint8_t>(p.data,p.data+p.size));
#if defined(OPENNOW_XDK)
    LeaveCriticalSection(&video_cs_);SetEvent(video_event_);
#endif
}
void WebRtcSession::decode_pending_video(){
#if defined(OPENNOW_XDK)
    return;
#else
    if(!decoder_||pending_video_units_.empty())return;std::vector<std::uint8_t>data;data.swap(pending_video_units_[0]);pending_video_units_.erase(pending_video_units_.begin());const std::uint64_t began=now_us();VideoFrame f;if(decoder_->decode(&data[0],data.size(),f)){const std::uint64_t decoded_at=now_us();++decoded_frames_;const bool shown=platform_.present(f);const std::uint64_t done=now_us();if(decoded_frames_==1||decoded_frames_%120==0)ON_LOGI("video-perf","frame=%llu decode_ms=%llu present_ms=%llu compressed=%u pending=%u shown=%d",decoded_frames_,(decoded_at-began)/1000ULL,(done-decoded_at)/1000ULL,(unsigned)data.size(),(unsigned)pending_video_units_.size(),shown?1:0);}else{++video_decode_failures_;request_keyframe_async();}
#endif
}
void WebRtcSession::present_ready_video(){
#if defined(OPENNOW_XDK)
    VideoFrame f;bool have=false;EnterCriticalSection(&frame_cs_);if(ready_video_available_){move_frame(f,ready_video_frame_);ready_video_available_=false;have=true;}LeaveCriticalSection(&frame_cs_);if(!have)return;const std::uint64_t began=now_us();bool shown=platform_.present(f);const std::uint64_t done=now_us();if(decoded_frames_==1||decoded_frames_%120==0)ON_LOGI("video-present","decoded=%llu present_ms=%llu shown=%d present_drops=%llu",decoded_frames_,(done-began)/1000ULL,shown?1:0,video_present_drops_);
#endif
}

bool WebRtcSession::submit_opus(const std::uint8_t*data,std::size_t size,bool recovered){std::vector<std::int16_t>pcm;int frames=0;if(!audio_decoder_->decode(data,size,pcm,frames)||frames<=0){++audio_decode_failures_;return false;}if(!platform_.play_pcm48_stereo(&pcm[0],(std::size_t)frames)){++audio_output_failures_;return false;}if(recovered)++audio_recovered_packets_;return true;}
bool WebRtcSession::submit_plc(){std::vector<std::int16_t>pcm;int frames=0;if(!audio_decoder_||!audio_decoder_->conceal(480,pcm,frames)||frames<=0){++audio_decode_failures_;return false;}if(!platform_.play_pcm48_stereo(&pcm[0],(std::size_t)frames)){++audio_output_failures_;return false;}++audio_concealed_packets_;return true;}
void WebRtcSession::process_audio_packet(const std::uint8_t*data,std::size_t size,std::uint16_t sequence,int payload_type){if(!audio_decoder_||!data||!size)return;const uint8_t*opus=data;size_t opus_size=size;if(payload_type==63){size_t header=0,redundant=0,red_lengths[8];int red_count=0;while(header<size&&(data[header]&0x80)!=0){if(size-header<4){++audio_decode_failures_;return;}const size_t block=((size_t)(data[header+2]&0x03)<<8)|data[header+3];if(red_count<8)red_lengths[red_count++]=block;redundant+=block;header+=4;}if(header>=size){++audio_decode_failures_;return;}++header;if(redundant>size-header){++audio_decode_failures_;return;}unsigned missing=0;if(audio_have_sequence_){const int delta=(int)(std::int16_t)(sequence-audio_last_sequence_);if(delta<=0)return;missing=(unsigned)(delta-1);}audio_have_sequence_=true;audio_last_sequence_=sequence;audio_missing_packets_+=missing;unsigned recover=std::min<unsigned>(missing,(unsigned)red_count),unrecovered=missing-recover;if(unrecovered){if(unrecovered<=6){for(unsigned i=0;i<unrecovered;++i)submit_plc();}else{audio_decoder_->reset();++audio_resyncs_;if(audio_resyncs_<=5||audio_resyncs_%50==0)ON_LOGW("audio-recovery","large gap=%u reset decoder resyncs=%llu",missing,audio_resyncs_);}}if(recover){const int first=red_count-(int)recover;size_t block_offset=header;for(int ri=0;ri<first;++ri)block_offset+=red_lengths[ri];for(int ri=first;ri<red_count;++ri){if(block_offset+red_lengths[ri]<=size)submit_opus(data+block_offset,red_lengths[ri],true);block_offset+=red_lengths[ri];}}if(missing&&(audio_recovered_packets_<=8||audio_recovered_packets_%100==0))ON_LOGI("audio-recovery","gap=%u red=%u plc_total=%llu recovered_total=%llu missing_total=%llu",missing,recover,audio_concealed_packets_,audio_recovered_packets_,audio_missing_packets_);opus=data+header+redundant;opus_size=size-header-redundant;}else{unsigned missing=0;if(audio_have_sequence_){const int delta=(int)(std::int16_t)(sequence-audio_last_sequence_);if(delta<=0)return;if(delta>1)missing=(unsigned)(delta-1);}audio_have_sequence_=true;audio_last_sequence_=sequence;audio_missing_packets_+=missing;if(missing){if(missing<=6){for(unsigned i=0;i<missing;++i)submit_plc();}else{audio_decoder_->reset();++audio_resyncs_;}}}if(!opus_size){++audio_decode_failures_;return;}submit_opus(opus,opus_size,false);}
void WebRtcSession::on_audio(const PeerAudioPacket&p){if(!audio_decoder_||!p.data||!p.size)return;++audio_packets_;
#if defined(OPENNOW_XDK)
    QueuedAudioPacket q;q.sequence=p.sequence;q.payload_type=(int)p.payload_type;q.data.assign(p.data,p.data+p.size);EnterCriticalSection(&audio_cs_);if(pending_audio_packets_.size()>=32){unsigned drop=(unsigned)(pending_audio_packets_.size()-15);pending_audio_packets_.erase(pending_audio_packets_.begin(),pending_audio_packets_.begin()+drop);audio_input_queue_drops_+=drop;}pending_audio_packets_.push_back(q);LeaveCriticalSection(&audio_cs_);SetEvent(audio_event_);
#else
    process_audio_packet(p.data,p.size,p.sequence,(int)p.payload_type);
#endif
}

bool WebRtcSession::send_gamepad(const GamepadState&s){if(!pc_||!input_ready_)return false;uint64_t ts=now_us();std::vector<std::uint8_t>raw=build_gamepad_payload(ts,s);std::vector<std::uint8_t>wire=wrap_reliable_gamepad(input_protocol_,ts,raw);lock_peer();int rc=(pc_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED)?peer_connection_datachannel_send_binary_sid(pc_,wire.empty()?NULL:(char*)&wire[0],wire.size(),0):-1;unlock_peer();if(rc<0)input_ready_=false;else ++input_packets_;return rc>=0;}
bool WebRtcSession::send_mouse_move(std::int16_t dx,std::int16_t dy){if((dx==0&&dy==0)||!pc_||!input_ready_)return false;uint64_t ts=now_us();std::vector<std::uint8_t>raw=build_mouse_move_payload(ts,dx,dy);std::vector<std::uint8_t>wire=input_protocol_<=2?raw:wrap_reliable_gamepad(input_protocol_,ts,raw);lock_peer();int rc=(pc_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED)?peer_connection_datachannel_send_binary_sid(pc_,(char*)&wire[0],wire.size(),0):-1;unlock_peer();if(rc>=0){++mouse_packets_;if(mouse_packets_<=8||mouse_packets_%300==0)ON_LOGI("input","mouse move tx count=%llu dx=%d dy=%d protocol=%d",mouse_packets_,dx,dy,input_protocol_);}return rc>=0;}
bool WebRtcSession::send_mouse_button(std::uint8_t button,bool pressed){if(!pc_||!input_ready_)return false;uint64_t ts=now_us();std::vector<std::uint8_t>wire=wrap_single_input(input_protocol_,ts,build_mouse_button_payload(ts,button,pressed));lock_peer();int rc=(pc_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED)?peer_connection_datachannel_send_binary_sid(pc_,(char*)&wire[0],wire.size(),0):-1;unlock_peer();if(rc>=0){++mouse_packets_;ON_LOGI("input","mouse button tx button=%u pressed=%d count=%llu",(unsigned)button,pressed?1:0,mouse_packets_);}return rc>=0;}
bool WebRtcSession::send_mouse_wheel(std::int16_t delta){if(!delta||!pc_||!input_ready_)return false;uint64_t ts=now_us();std::vector<std::uint8_t>wire=wrap_single_input(input_protocol_,ts,build_mouse_wheel_payload(ts,delta));lock_peer();int rc=(pc_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED)?peer_connection_datachannel_send_binary_sid(pc_,(char*)&wire[0],wire.size(),0):-1;unlock_peer();if(rc>=0){++mouse_packets_;ON_LOGI("input","mouse wheel tx delta=%d count=%llu",delta,mouse_packets_);}return rc>=0;}
bool WebRtcSession::send_key(const KeyboardStroke&key,bool pressed){if(!pc_||!input_ready_)return false;uint64_t ts=now_us();std::vector<std::uint8_t>wire=wrap_single_input(input_protocol_,ts,build_keyboard_payload(ts,key,pressed));lock_peer();int rc=(pc_&&peer_connection_get_state(pc_)==PEER_CONNECTION_COMPLETED)?peer_connection_datachannel_send_binary_sid(pc_,(char*)&wire[0],wire.size(),0):-1;unlock_peer();if(rc>=0)++key_packets_;return rc>=0;}
bool WebRtcSession::send_text(const std::string&text,bool enter){unsigned sent=0,skipped=0;for(size_t i=0;i<text.size();++i){KeyboardStroke k;if(map_ascii_key(text[i],k)){send_key(k,true);send_key(k,false);++sent;}else ++skipped;}if(enter){KeyboardStroke k;if(map_ascii_key('\n',k)){send_key(k,true);send_key(k,false);}}ON_LOGI("input","keyboard text submitted chars=%u skipped=%u enter=%d",sent,skipped,enter?1:0);return skipped==0;}
void WebRtcSession::ice_cb(char*s,void*u){if(u&&s)((WebRtcSession*)u)->on_ice(s);}void WebRtcSession::state_cb(PeerConnectionState s,void*u){if(u)((WebRtcSession*)u)->on_state((int)s);}void WebRtcSession::data_cb(char*m,size_t n,void*u,uint16_t sid){if(u)((WebRtcSession*)u)->on_data(m,n,sid);}void WebRtcSession::open_cb(void*u){if(u)((WebRtcSession*)u)->on_open();}void WebRtcSession::close_cb(void*u){if(u)((WebRtcSession*)u)->on_close();}void WebRtcSession::video_cb(const PeerVideoPacket*p,void*u){if(u&&p)((WebRtcSession*)u)->on_video(*p);}void WebRtcSession::audio_cb(const PeerAudioPacket*p,void*u){if(u&&p)((WebRtcSession*)u)->on_audio(*p);}
}
