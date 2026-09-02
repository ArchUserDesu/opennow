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
    return (uint64_t)GetTickCount() * 1000ULL;
#else
    return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
std::string int_text(long long v){char b[48];std::sprintf(b,"%lld",v);return b;}
std::string make_peer(){char b[40];std::sprintf(b,"peer-%llu",(unsigned long long)(now_us()%10000000000ULL));return b;}
std::string signin_url(std::string u,const std::string& peer,const std::string& sid){std::string::size_type q=u.find('?');if(q!=std::string::npos)u.resize(q);if(u.compare(0,8,"https://")==0)u.replace(0,5,"wss");else if(u.compare(0,7,"http://")==0)u.replace(0,4,"ws");while(!u.empty()&&u[u.size()-1]=='/')u.erase(u.size()-1);if(u.size()<8||u.substr(u.size()-8)!="sign_in")u+="/sign_in";return u+"?peer_id="+peer+"&version=2&peer_role=1&pairing_id="+sid;}
std::vector<std::string> sdp_lines(const std::string&s){std::vector<std::string>o;size_t p=0;while(p<s.size()){size_t e=s.find('\n',p);std::string l=s.substr(p,e==std::string::npos?std::string::npos:e-p);if(!l.empty()&&l[l.size()-1]=='\r')l.erase(l.size()-1);o.push_back(l);if(e==std::string::npos)break;p=e+1;}return o;}
std::string candidate_only(std::string s){if(s.compare(0,12,"a=candidate:")==0)s.erase(0,2);while(!s.empty()&&(s[s.size()-1]=='\r'||s[s.size()-1]=='\n'))s.erase(s.size()-1);return s;}
bool contains_idr(const uint8_t*d,size_t n){if(!d)return false;for(size_t i=0;i+4<n;i++){size_t h=0;if(d[i]==0&&d[i+1]==0&&d[i+2]==1)h=i+3;else if(i+4<n&&d[i]==0&&d[i+1]==0&&d[i+2]==0&&d[i+3]==1)h=i+4;if(h&&h<n&&(d[h]&31)==5)return true;}return false;}
}

WebRtcSession::WebRtcSession(const SessionInfo&i,const StreamConfig&c,XenonPlatform&p)
:info_(i),cfg_(c),platform_(p),pc_(NULL),decoder_(NULL),audio_decoder_(NULL),peer_name_(make_peer()),peer_id_(0),remote_peer_id_(1),ack_(0),running_(false),remote_set_(false),answer_sent_(false),offer_seen_(false),sctp_open_(false),channel_requested_(false),input_ready_(false),manual_candidate_added_(false),input_protocol_(2),gamepad_seq_(1),state_("idle"),last_hb_us_(0),last_peer_info_us_(0),last_input_hb_us_(0),channel_request_us_(0),input_activation_us_(0),manual_candidate_due_us_(0),last_pli_us_(0),video_packets_(0),audio_packets_(0),decoded_frames_(0),video_decode_failures_(0),audio_decode_failures_(0),input_packets_(0){}
WebRtcSession::~WebRtcSession(){stop();}

bool WebRtcSession::start(){
    ON_LOGI("webrtc","start begin target=%dx%d fps=%d ice_servers=%u",cfg_.width,cfg_.height,cfg_.fps,(unsigned)info_.ice_servers.size());
    decoder_=make_ffmpeg_h264_decoder();
    if(!decoder_||!decoder_->open(cfg_.width,cfg_.height,cfg_.fps)){state_="H.264 software decoder init failed";ON_LOGE("webrtc","%s",state_.c_str());delete decoder_;decoder_=NULL;return false;}
    audio_decoder_=make_opus_audio_decoder();
    if(!audio_decoder_||!audio_decoder_->open(48000,2)){state_="Opus software decoder init failed";ON_LOGE("webrtc","%s",state_.c_str());delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    int peer_rc=peer_init();
    if(peer_rc!=0){state_="libpeer initialization failed";ON_LOGE("webrtc","%s rc=%d",state_.c_str(),peer_rc);delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    PeerConfiguration c;std::memset(&c,0,sizeof(c));c.video_codec=CODEC_H264;c.audio_codec=CODEC_OPUS;c.datachannel=DATA_CHANNEL_STRING;c.onvideopacket=&video_cb;c.onaudiopacket=&audio_cb;c.user_data=this;
    if(info_.ice_servers.empty())info_.ice_servers.push_back(IceServerInfo("stun:s1.stun.gamestream.nvidia.com:19308","",""));
    const size_t n=std::min((size_t)5,info_.ice_servers.size());
    for(size_t k=0;k<n;k++){c.ice_servers[k].urls=info_.ice_servers[k].url.c_str();c.ice_servers[k].username=info_.ice_servers[k].username.empty()?NULL:info_.ice_servers[k].username.c_str();c.ice_servers[k].credential=info_.ice_servers[k].credential.empty()?NULL:info_.ice_servers[k].credential.c_str();}
    pc_=peer_connection_create(&c);
    if(!pc_){state_="peer_connection_create failed";peer_deinit();delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    peer_connection_onicecandidate(pc_,&ice_cb);peer_connection_oniceconnectionstatechange(pc_,&state_cb);peer_connection_ondatachannel(pc_,&data_cb,&open_cb,&close_cb);
    std::vector<std::string> h;h.push_back("Origin: https://play.geforcenow.com");h.push_back("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/131.0.0.0 Safari/537.36");h.push_back("Sec-WebSocket-Protocol: x-nv-sessionid."+info_.session_id);
    if(!ws_.connect(signin_url(info_.signaling_url,peer_name_,info_.session_id),h)){state_="signaling: "+ws_.error();peer_connection_destroy(pc_);pc_=NULL;peer_deinit();delete audio_decoder_;audio_decoder_=NULL;delete decoder_;decoder_=NULL;return false;}
    running_=true;state_="signaling connected";send_peer_info();heartbeat();return true;
}

void WebRtcSession::stop(){if(!pc_&&!running_&&!decoder_&&!audio_decoder_)return;running_=false;ws_.close();if(pc_){peer_connection_close(pc_);peer_connection_destroy(pc_);pc_=NULL;peer_deinit();}if(decoder_)decoder_->flush();delete decoder_;decoder_=NULL;if(audio_decoder_)audio_decoder_->reset();delete audio_decoder_;audio_decoder_=NULL;state_="stopped";}
int WebRtcSession::rtt_ms()const{return pc_?peer_connection_get_rtt_ms(pc_):-1;}

bool WebRtcSession::poll(){if(!running_)return false;std::vector<std::string> ms;if(!ws_.poll(ms)){state_="signaling disconnected: "+ws_.error();running_=false;return false;}for(size_t i=0;i<ms.size();++i)handle_signal(ms[i]);maybe_keepalive();if(pc_&&remote_set_){for(int i=0;i<24;i++)if(peer_connection_loop(pc_)==0)break;}maybe_manual_candidate();maybe_open_input();return running_;}

void WebRtcSession::send_peer_info(){json_t*r=json_object(),*p=json_object();json_object_set_new(r,"ackid",json_integer(++ack_));json_object_set_new(p,"browser",json_string("Chrome"));json_object_set_new(p,"browserVersion",json_string("131"));json_object_set_new(p,"connected",json_true());json_object_set_new(p,"id",json_integer(peer_id_));json_object_set_new(p,"name",json_string(peer_name_.c_str()));json_object_set_new(p,"peerRole",json_integer(0));std::string res=int_text(cfg_.width)+"x"+int_text(cfg_.height);json_object_set_new(p,"resolution",json_string(res.c_str()));json_object_set_new(p,"version",json_integer(2));json_object_set_new(r,"peer_info",p);ws_.send_text(dump_json(r));json_decref(r);last_peer_info_us_=now_us();}
void WebRtcSession::heartbeat(){json_t*r=json_object();json_object_set_new(r,"hb",json_integer(1));ws_.send_text(dump_json(r));json_decref(r);last_hb_us_=now_us();}
void WebRtcSession::maybe_keepalive(){uint64_t n=now_us();if(!last_hb_us_||n-last_hb_us_>=5000000ULL)heartbeat();if(!offer_seen_&&(!last_peer_info_us_||n-last_peer_info_us_>=2000000ULL))send_peer_info();}
void WebRtcSession::send_peer_payload(void* vp){json_t*payload=(json_t*)vp;char*inner=json_dumps(payload,JSON_COMPACT);json_t*r=json_object(),*pm=json_object();json_object_set_new(pm,"from",json_integer(peer_id_));json_object_set_new(pm,"to",json_integer(remote_peer_id_));json_object_set_new(pm,"msg",json_string(inner?inner:"{}"));json_object_set_new(r,"peer_msg",pm);json_object_set_new(r,"ackid",json_integer(++ack_));ws_.send_text(dump_json(r));if(inner)free(inner);json_decref(r);}

void WebRtcSession::handle_signal(const std::string&m){
    JsonPtr root(NULL,&json_decref);try{root=parse_json(m);}catch(const std::exception&e){ON_LOGW("signaling","ignored invalid outer JSON bytes=%u error=%s",(unsigned)m.size(),e.what());return;}catch(...){return;}
    json_t*pi=json_object_get(root.get(),"peer_info");if(json_is_object(pi)&&js(pi,"name")==peer_name_)peer_id_=ji(pi,"id",peer_id_);
    json_t*aid=json_object_get(root.get(),"ackid");if(json_is_integer(aid)){bool should=true;if(json_is_object(pi)&&ji(pi,"id",-1)==peer_id_)should=false;if(should){json_t*a=json_object();json_object_set_new(a,"ack",json_integer(json_integer_value(aid)));ws_.send_text(dump_json(a));json_decref(a);}}
    if(json_object_get(root.get(),"hb")){heartbeat();return;}
    json_t*pm=json_object_get(root.get(),"peer_msg");if(!json_is_object(pm))return;remote_peer_id_=ji(pm,"from",remote_peer_id_);std::string raw=js(pm,"msg");if(raw.empty())return;
    JsonPtr p(NULL,&json_decref);try{p=parse_json(raw);}catch(...){return;}
    if(js(p.get(),"type")=="offer"&&pc_){
        offer_seen_=true;std::string offer=prepare_gfn_offer(js(p.get(),"sdp"),info_.signaling_url,info_.media_ip);RiInputCaps caps=parse_ri_caps(offer);input_protocol_=2;
        peer_connection_set_remote_description(pc_,offer.c_str(),SDP_TYPE_OFFER);remote_set_=true;const char*ans=peer_connection_create_answer(pc_);
        if(ans){std::string adapted=adapt_gfn_answer(ans,offer,cfg_),nvst=build_nvst_sdp(adapted,cfg_,caps);json_t*a=json_object();json_object_set_new(a,"type",json_string("answer"));json_object_set_new(a,"sdp",json_string(adapted.c_str()));json_object_set_new(a,"nvstSdp",json_string(nvst.c_str()));send_peer_payload(a);json_decref(a);answer_sent_=true;for(size_t i=0;i<local_candidates_.size();++i)on_ice(local_candidates_[i]);local_candidates_.clear();state_="answer sent";manual_candidate_due_us_=now_us()+900000ULL;}
    }else{std::string c=js(p.get(),"candidate");if(!c.empty()&&pc_){if(c.compare(0,12,"a=candidate:")!=0)c="a="+c;int rc=peer_connection_add_ice_candidate(pc_,const_cast<char*>(c.c_str()));if(rc==0)state_="remote ICE";}}
}

void WebRtcSession::on_ice(const std::string&s){if(!answer_sent_){local_candidates_.push_back(s);return;}bool any=false;std::vector<std::string> lines=sdp_lines(s);for(size_t i=0;i<lines.size();++i){const std::string&l=lines[i];if(l.compare(0,12,"a=candidate:")!=0)continue;any=true;json_t*c=json_object();std::string v=candidate_only(l);json_object_set_new(c,"candidate",json_string(v.c_str()));json_object_set_new(c,"sdpMid",json_string("0"));json_object_set_new(c,"sdpMLineIndex",json_integer(0));send_peer_payload(c);json_decref(c);}if(!any&&s.compare(0,12,"a=candidate:")==0){json_t*c=json_object();std::string v=candidate_only(s);json_object_set_new(c,"candidate",json_string(v.c_str()));json_object_set_new(c,"sdpMid",json_string("0"));json_object_set_new(c,"sdpMLineIndex",json_integer(0));send_peer_payload(c);json_decref(c);}}
void WebRtcSession::maybe_manual_candidate(){if(!pc_||manual_candidate_added_||!manual_candidate_due_us_||now_us()<manual_candidate_due_us_)return;PeerConnectionState st=peer_connection_get_state(pc_);if(st==PEER_CONNECTION_COMPLETED){manual_candidate_added_=true;return;}std::string c=build_manual_candidate(info_.signaling_url,info_.media_ip,info_.media_port,1);if(!c.empty()){int rc=peer_connection_add_ice_candidate(pc_,const_cast<char*>(c.c_str()));ON_LOGW("webrtc","manual ICE fallback rc=%d",rc);state_="fallback ICE added";}manual_candidate_added_=true;}
void WebRtcSession::on_state(int st){state_=pc_?peer_connection_state_to_string((PeerConnectionState)st):"closed";if(st==PEER_CONNECTION_FAILED||st==PEER_CONNECTION_CLOSED||st==PEER_CONNECTION_DISCONNECTED)running_=false;}
void WebRtcSession::maybe_open_input(){if(!pc_||peer_connection_get_state(pc_)!=PEER_CONNECTION_COMPLETED)return;uint64_t n=now_us();if(!channel_requested_&&(!channel_request_us_||n-channel_request_us_>=750000ULL)){channel_request_us_=n;char label[]="input_channel_v1",protocol[]="";int r=peer_connection_create_datachannel_sid(pc_,DATA_CHANNEL_RELIABLE,0,0,label,protocol,0);if(r>=0){channel_requested_=true;input_activation_us_=n+1500000ULL;state_="input channel requested";}}if(channel_requested_&&sctp_open_&&!input_ready_&&input_activation_us_&&n>=input_activation_us_){input_protocol_=2;input_ready_=true;state_="input ready v2 fallback";}if(input_ready_&&(!last_input_hb_us_||n-last_input_hb_us_>=2000000ULL)){uint8_t hb[4]={2,0,0,0};peer_connection_datachannel_send_binary_sid(pc_,(char*)hb,sizeof(hb),0);last_input_hb_us_=n;}}
void WebRtcSession::on_data(char*msg,size_t len,uint16_t sid){if(!msg||len<2)return;const uint8_t*b=(const uint8_t*)msg;uint16_t first=(uint16_t)(b[0]|(b[1]<<8));if(sid==0){int v=2;bool hs=false;if(first==526){v=len>=4?(int)(b[2]|(b[3]<<8)):2;hs=true;}else if(b[0]==0x0e){v=first;hs=true;}if(hs){input_protocol_=std::max(2,v);input_ready_=true;state_="input handshake v"+int_text(input_protocol_);}}}
void WebRtcSession::on_open(){sctp_open_=true;} void WebRtcSession::on_close(){sctp_open_=false;channel_requested_=false;input_ready_=false;channel_request_us_=now_us();}
void WebRtcSession::on_video(const PeerVideoPacket&p){if(!decoder_||!p.data||!p.size)return;++video_packets_;VideoFrame f;if(decoder_->decode(p.data,p.size,f)){++decoded_frames_;platform_.present(f);}else{++video_decode_failures_;if(!contains_idr(p.data,p.size)&&pc_&&now_us()-last_pli_us_>1500000ULL){last_pli_us_=now_us();peer_connection_request_video_keyframe(pc_);}}}
void WebRtcSession::on_audio(const PeerAudioPacket&p){if(!audio_decoder_||!p.data||!p.size)return;++audio_packets_;std::vector<std::int16_t>pcm;int frames=0;if(audio_decoder_->decode(p.data,p.size,pcm,frames)&&frames>0){platform_.play_pcm48_stereo(&pcm[0],(std::size_t)frames);}else ++audio_decode_failures_;}
bool WebRtcSession::send_gamepad(const GamepadState&s){if(!pc_||!input_ready_||peer_connection_get_state(pc_)!=PEER_CONNECTION_COMPLETED)return false;uint64_t ts=now_us();std::vector<std::uint8_t>raw=build_gamepad_payload(ts,s);std::vector<std::uint8_t>wire=wrap_reliable_gamepad(input_protocol_,ts,raw);int rc=peer_connection_datachannel_send_binary_sid(pc_,wire.empty()?NULL:(char*)&wire[0],wire.size(),0);if(rc<0)input_ready_=false;else ++input_packets_;return rc>=0;}
void WebRtcSession::ice_cb(char*s,void*u){if(u&&s)((WebRtcSession*)u)->on_ice(s);}void WebRtcSession::state_cb(PeerConnectionState s,void*u){if(u)((WebRtcSession*)u)->on_state((int)s);}void WebRtcSession::data_cb(char*m,size_t n,void*u,uint16_t sid){if(u)((WebRtcSession*)u)->on_data(m,n,sid);}void WebRtcSession::open_cb(void*u){if(u)((WebRtcSession*)u)->on_open();}void WebRtcSession::close_cb(void*u){if(u)((WebRtcSession*)u)->on_close();}void WebRtcSession::video_cb(const PeerVideoPacket*p,void*u){if(u&&p)((WebRtcSession*)u)->on_video(*p);}void WebRtcSession::audio_cb(const PeerAudioPacket*p,void*u){if(u&&p)((WebRtcSession*)u)->on_audio(*p);}
}
