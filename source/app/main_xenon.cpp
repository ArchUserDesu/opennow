#include "opennow/gfn_client.hpp"
#include "opennow/http_client.hpp"
#include "opennow/json_util.hpp"
#include "opennow/persistence.hpp"
#include "opennow/webrtc_session.hpp"
#include "opennow/xenon_platform.hpp"
#include "opennow/logger.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>
#include <stdexcept>
#include <vector>

#ifdef __LIBXENON__
extern "C" {
#include <time/time.h>
}
#elif defined(OPENNOW_XDK)
#include <xtl.h>
#endif

namespace {
using namespace opennow;

#ifdef OPENNOW_XDK
static const char* const kSessionFile = "game:\\opennow_session.json";
#else
static const char* const kSessionFile = "uda:/opennow_session.json";
#endif
static const char* const kDeviceClient = "q61ddeJrVt7O90Nl-P-N7I36yctih4Ml6FyXLrb6j-U";
static const char* const kDeviceUserAgent = "Mozilla/5.0 (X11; Linux x86_64; Steam Deck) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";

void sleep_ms(int ms) {
#ifdef __LIBXENON__
    mdelay(ms);
#elif defined(OPENNOW_XDK)
    Sleep((DWORD)ms);
#else
    (void)ms;
#endif
}

std::int64_t auth_now_ms(){return (std::int64_t)time(NULL)*1000;}
bool auth_near_expiry(const AuthSession&s){return s.tokens.expires_at_ms<=0||s.tokens.expires_at_ms<=auth_now_ms()+10LL*60LL*1000LL;}
std::vector<std::string> token_headers(const std::string& bearer,bool form){
    std::vector<std::string> h;
    h.push_back("Origin: https://play.geforcenow.com");h.push_back("Referer: https://play.geforcenow.com/");h.push_back("Accept: application/json, text/plain, */*");h.push_back(std::string("User-Agent: ")+kDeviceUserAgent);
    if(!bearer.empty())h.push_back("Authorization: Bearer "+bearer);
    if(form)h.push_back("Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    return h;
}
void ensure_client_token(AuthSession&s){
    if(s.tokens.access_token.empty()||!s.tokens.client_token.empty())return;
    try{
        HttpClient http;HttpResponse r=http.get("https://login.nvidia.com/client_token",token_headers(s.tokens.access_token,false));
        if(r.status_code!=200){ON_LOGW("auth","client-token acquisition HTTP=%d",r.status_code);return;}
        JsonPtr j=parse_json(r.body);std::string t=js(j.get(),"client_token");if(t.empty()){ON_LOGW("auth","client-token acquisition returned no token");return;}
        s.tokens.client_token=t;s.tokens.client_token_expires_at_ms=auth_now_ms()+(std::int64_t)ji(j.get(),"expires_in",86400)*1000LL;
        ON_LOGI("auth","client-token acquired expires_in_s=%d",ji(j.get(),"expires_in",86400));
    }catch(const std::exception&e){ON_LOGW("auth","client-token acquisition failed: %s",e.what());}
}
AuthSession refresh_auth_resilient(GfnClient&g,AuthSession s){
    if(!auth_near_expiry(s)){ensure_client_token(s);return s;}
    if(!s.tokens.client_token.empty()&&!s.user.user_id.empty()){
        try{
            const std::string cid=s.tokens.auth_client_id.empty()?kDeviceClient:s.tokens.auth_client_id;
            const std::string body="grant_type="+HttpClient::form_escape("urn:ietf:params:oauth:grant-type:client_token")+"&client_token="+HttpClient::form_escape(s.tokens.client_token)+"&client_id="+HttpClient::form_escape(cid)+"&sub="+HttpClient::form_escape(s.user.user_id);
            HttpClient http;HttpResponse r=http.post("https://login.nvidia.com/token",token_headers("",true),body);
            if(r.status_code==200){
                JsonPtr j=parse_json(r.body);std::string access=js(j.get(),"access_token");if(access.empty())throw std::runtime_error("client-token refresh returned no access_token");
                const std::string old_ref=s.tokens.refresh_token,old_id=s.tokens.id_token,old_client=s.tokens.client_token;
                s.tokens.access_token=access;s.tokens.refresh_token=js(j.get(),"refresh_token");s.tokens.id_token=js(j.get(),"id_token");s.tokens.client_token=js(j.get(),"client_token");s.tokens.auth_client_id=cid;
                if(s.tokens.refresh_token.empty())s.tokens.refresh_token=old_ref;if(s.tokens.id_token.empty())s.tokens.id_token=old_id;if(s.tokens.client_token.empty())s.tokens.client_token=old_client;
                s.tokens.expires_at_ms=auth_now_ms()+(std::int64_t)ji(j.get(),"expires_in",86400)*1000LL;
                ON_LOGI("auth","client-token refresh succeeded expires_in_s=%d",ji(j.get(),"expires_in",86400));return s;
            }
            ON_LOGW("auth","client-token refresh HTTP=%d; falling back to OAuth refresh",r.status_code);
        }catch(const std::exception&e){ON_LOGW("auth","client-token refresh failed; falling back error=%s",e.what());}
    }
    s=g.refresh(s);ensure_client_token(s);return s;
}

void ui_printf(XenonPlatform& platform, const char* format, ...) {
    char buffer[2048];va_list args;va_start(args,format);
#ifdef OPENNOW_XDK
    _vsnprintf(buffer,sizeof(buffer)-1,format,args);buffer[sizeof(buffer)-1]='\0';
#else
    std::vsnprintf(buffer,sizeof(buffer),format,args);
#endif
    va_end(args);platform.write_text(buffer);
}
void wait_release(XenonPlatform&p){GamepadState s;for(int i=0;i<50;++i){p.poll();if(!p.read_gamepad(s)||s.buttons==0)return;sleep_ms(20);}}
int choose(XenonPlatform&p,const std::vector<std::string>&labels,const char*title){if(labels.empty())return-1;int idx=0,last=-1;for(;;){if(idx!=last){p.clear_text();ui_printf(p,"=== %s ===\n\n",title);const int first=(idx/10)*10;for(int i=first;i<std::min<int>(first+10,labels.size());++i)ui_printf(p,"%c %d. %s\n",i==idx?'>':' ',i+1,labels[i].c_str());ui_printf(p,"\nD-pad: select   A: confirm   B: cancel\n");last=idx;}p.poll();GamepadState g;if(p.read_gamepad(g)){if(g.buttons&1){idx=(idx-1+(int)labels.size())%(int)labels.size();wait_release(p);}else if(g.buttons&2){idx=(idx+1)%(int)labels.size();wait_release(p);}else if(g.buttons&0x1000){wait_release(p);return idx;}else if(g.buttons&0x2000){wait_release(p);return-1;}}sleep_ms(20);}}
LoginProvider select_provider(GfnClient&g,XenonPlatform&p){std::vector<LoginProvider>providers=g.fetch_login_providers();if(providers.empty())throw std::runtime_error("No GFN login providers returned");std::vector<std::string>names;for(size_t i=0;i<providers.size();++i)names.push_back(providers[i].display_name+" ["+providers[i].code+"]");int n=choose(p,names,"GeForce NOW provider");if(n<0)throw std::runtime_error("provider selection cancelled");return providers[n];}
struct GameSort{bool operator()(const GameInfo&a,const GameInfo&b)const{const bool am=a.title.find("Marvel Rivals")!=std::string::npos,bm=b.title.find("Marvel Rivals")!=std::string::npos;if(am!=bm)return am;return a.title<b.title;}};
void make_names(const std::vector<GameInfo>&games,std::vector<std::string>&names){names.clear();names.reserve(games.size());for(size_t i=0;i<games.size();++i)names.push_back(games[i].title+(games[i].store.empty()?"":" ["+games[i].store+"]"));}
GameInfo resolve_game(GfnClient&g,AuthSession&auth,const GameInfo&selected){std::vector<GameInfo>found=g.fetch_catalog_games(auth,selected.title);if(found.empty())return selected;for(size_t i=0;i<found.size();++i)if(found[i].title==selected.title)return found[i];return found[0];}
GameInfo select_game(GfnClient&g,AuthSession&auth,XenonPlatform&p){
    p.clear_text();ui_printf(p,"Loading GeForce NOW game list...\n");
#ifdef OPENNOW_XDK
    DWORD began=GetTickCount();
#endif
    std::vector<GameInfo>games=g.fetch_public_games();
    if(games.empty()){ON_LOGW("catalog","fast public catalog empty; falling back to authenticated catalog");games=g.fetch_catalog_games(auth);}
    std::stable_sort(games.begin(),games.end(),GameSort());std::vector<std::string>names;make_names(games,names);
#ifdef OPENNOW_XDK
    ON_LOGI("catalog","fast all-games list ready count=%u elapsed_ms=%u",(unsigned)games.size(),(unsigned)(GetTickCount()-began));
#else
    ON_LOGI("catalog","fast all-games list ready count=%u",(unsigned)games.size());
#endif
    std::vector<GameInfo>library;std::vector<std::string>library_names;bool library_loaded=false,library_tab=false;int indices[2]={0,0};GameInfo game;bool selected=false;
    for(;;){
        if(library_tab&&!library_loaded){p.clear_text();ui_printf(p,"Loading My Library on demand...\nThis is only fetched when you open the Library tab.\n");
#ifdef OPENNOW_XDK
            DWORD lb=GetTickCount();
#endif
            std::vector<GameInfo>auth_games=g.fetch_catalog_games(auth);for(size_t i=0;i<auth_games.size();++i)if(auth_games[i].in_library)library.push_back(auth_games[i]);std::stable_sort(library.begin(),library.end(),GameSort());make_names(library,library_names);library_loaded=true;
#ifdef OPENNOW_XDK
            ON_LOGI("catalog","lazy library ready library=%u scanned=%u elapsed_ms=%u",(unsigned)library.size(),(unsigned)auth_games.size(),(unsigned)(GetTickCount()-lb));
#else
            ON_LOGI("catalog","lazy library ready library=%u scanned=%u",(unsigned)library.size(),(unsigned)auth_games.size());
#endif
        }
        const std::vector<GameInfo>&tab_games=library_tab?library:games;const std::vector<std::string>&tab_names=library_tab?library_names:names;int&idx=indices[library_tab?1:0];if(!tab_games.empty()&&idx>=(int)tab_games.size())idx=(int)tab_games.size()-1;
        p.clear_text();ui_printf(p,"LB/RB  [ALL GAMES]%s  [MY LIBRARY]%s\n",library_tab?"":" <",library_tab?" <":"");ui_printf(p,"%s: %u games   Page %u/%u\n",library_tab?"My Library":"All Games",(unsigned)tab_games.size(),tab_games.empty()?0u:(unsigned)(idx/20+1),tab_games.empty()?0u:(unsigned)((tab_games.size()+19)/20));
        if(tab_games.empty())ui_printf(p,"No games in this tab. Use LB/RB to switch.\n");else{const int first=(idx/20)*20;for(int i=first;i<std::min<int>(first+20,tab_names.size());++i)ui_printf(p,"%c %d. %s\n",i==idx?'>':' ',i+1,tab_names[i].c_str());}ui_printf(p,"UP/DOWN: select  LEFT/RIGHT: skip 20  A: play  B: cancel\n");
        bool redraw=false;for(;;){p.poll();GamepadState pad;if(p.read_gamepad(pad)){if((pad.buttons&0x0100)||(pad.buttons&0x0200)){library_tab=!library_tab;ON_LOGI("catalog","tab changed tab=%s lazy_loaded=%d",library_tab?"library":"all",library_loaded?1:0);wait_release(p);redraw=true;break;}if(!tab_games.empty()&&(pad.buttons&1)){idx=(idx-1+(int)tab_games.size())%(int)tab_games.size();wait_release(p);redraw=true;break;}if(!tab_games.empty()&&(pad.buttons&2)){idx=(idx+1)%(int)tab_games.size();wait_release(p);redraw=true;break;}if(!tab_games.empty()&&(pad.buttons&4)){const int count=(int)tab_games.size();idx=(idx-(20%count)+count)%count;wait_release(p);redraw=true;break;}if(!tab_games.empty()&&(pad.buttons&8)){idx=(idx+20)%(int)tab_games.size();wait_release(p);redraw=true;break;}if(!tab_games.empty()&&(pad.buttons&0x1000)){game=tab_games[idx];wait_release(p);selected=true;break;}if(pad.buttons&0x2000){wait_release(p);throw std::runtime_error("game selection cancelled");}}sleep_ms(20);}if(selected)break;if(redraw)continue;
    }
    game=resolve_game(g,auth,game);
    if(game.variants.size()>1){std::vector<std::string>variants;for(size_t i=0;i<game.variants.size();++i){const GameVariant&v=game.variants[i];variants.push_back((v.store.empty()?"variant":v.store)+"  id="+v.id);}const int vi=choose(p,variants,"Select store/variant");if(vi>=0){game.launch_app_id=game.variants[vi].id;game.store=game.variants[vi].store;game.internal_title=game.variants[vi].internal_title;}}
    if(game.launch_app_id.empty())throw std::runtime_error("Selected game has no launch app id");return game;
}

const char* const kKeyboardChars="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@.-_/ ";const int kKeyboardCharCount=42,kKeyboardBackspace=42,kKeyboardSend=43,kKeyboardCancel=44;
void render_local_keyboard(XenonPlatform&platform,int selected,const std::string&entered){char overlay[1536];int used=_snprintf(overlay,sizeof(overlay),"OPENNOW KEYBOARD\n\nTEXT: %s\n\n",entered.empty()?"(EMPTY)":entered.c_str());for(int row=0;row<7&&used>0&&used<(int)sizeof(overlay);++row){for(int col=0;col<6&&used>0&&used<(int)sizeof(overlay);++col){int index=row*6+col;used+=_snprintf(overlay+used,sizeof(overlay)-used,index==selected?"[%c] ":" %c  ",kKeyboardChars[index]);}if(used>0&&used<(int)sizeof(overlay))used+=_snprintf(overlay+used,sizeof(overlay)-used,"\n");}if(used>0&&used<(int)sizeof(overlay))_snprintf(overlay+used,sizeof(overlay)-used,"\n%sBACKSPACE%s  %sSEND%s  %sCANCEL%s\n\nDPAD: MOVE    A: SELECT    B: ERASE\nSTART: SEND    Y: CANCEL",selected==kKeyboardBackspace?"[":" ",selected==kKeyboardBackspace?"]":" ",selected==kKeyboardSend?"[":" ",selected==kKeyboardSend?"]":" ",selected==kKeyboardCancel?"[":" ",selected==kKeyboardCancel?"]":" ");overlay[sizeof(overlay)-1]=0;platform.set_stream_overlay(overlay);}
} // namespace

#if defined(__LIBXENON__) || defined(OPENNOW_XDK)
int main(){
    opennow::XenonPlatform platform;if(!platform.init()){ON_LOGF("main","platform initialization failed; aborting before GFN startup");platform.clear_text();ui_printf(platform,"OpenNOW: platform/network initialization failed.\nSee opennow.log for details.\n");opennow::log_close();sleep_ms(5000);return 1;}
    if(!opennow::input_encoding_self_test()){ON_LOGF("input","GFN input packet encoding self-test failed");platform.clear_text();ui_printf(platform,"OpenNOW input protocol self-test failed.\nSee opennow.log.\n");opennow::log_close();return 1;}ON_LOGI("input","GFN gamepad mouse keyboard packet encoding self-test passed");ON_LOGI("main","entered application main; persistent_log=%s",opennow::log_path());platform.clear_text();ui_printf(platform,"OpenNOW Xbox 360 XEX - CPU H.264 build\nRunning under the Xbox kernel; console network configuration is used.\n\n");
    opennow::GfnClient gfn;opennow::AuthSession auth;
    try{
        ON_LOGI("main","checking saved authentication session at %s",kSessionFile);if(load_session(auth,kSessionFile)){ON_LOGI("auth","saved session loaded provider=%s user_name_length=%u refresh=%d client_token=%d",auth.provider.code.c_str(),(unsigned)auth.user.display_name.size(),auth.tokens.refresh_token.empty()?0:1,auth.tokens.client_token.empty()?0:1);ui_printf(platform,"Loaded saved GFN login for %s\n",auth.user.display_name.c_str());try{auth=refresh_auth_resilient(gfn,auth);const bool saved=save_session(auth,kSessionFile);ON_LOGI("auth","saved session renewal complete persisted=%d refresh=%d client_token=%d",saved?1:0,auth.tokens.refresh_token.empty()?0:1,auth.tokens.client_token.empty()?0:1);}catch(const std::exception&e){ON_LOGE("auth","saved session renewal failed: %s",e.what());ui_printf(platform,"Saved session expired. Starting QR login.\n");auth=opennow::AuthSession();}}else ON_LOGI("auth","no usable saved session found");
        if(auth.tokens.access_token.empty()){ON_LOGI("auth","fetching login providers");LoginProvider provider=select_provider(gfn,platform);auth=gfn.login_qr(provider,[&](const opennow::QrLoginChallenge&q){ON_LOGI("auth","QR challenge received interval=%d expires_at_ms=%lld",q.interval_seconds,(long long)q.expires_at_ms);platform.clear_text();ui_printf(platform,"SIGN IN TO GEFORCE NOW\n\nOpen: %s\n\nCode: %s\n\nDirect URL:\n%s\n\nWaiting for authorization...\nB: cancel\n",q.verification_uri.c_str(),q.user_code.c_str(),q.verification_uri_complete.c_str());},[&]()->bool{platform.poll();opennow::GamepadState s;return platform.read_gamepad(s)&&(s.buttons&0x2000);});ensure_client_token(auth);const bool saved=save_session(auth,kSessionFile);ON_LOGI("auth","QR login complete session_persisted=%d refresh=%d client_token=%d",saved?1:0,auth.tokens.refresh_token.empty()?0:1,auth.tokens.client_token.empty()?0:1);platform.clear_text();ui_printf(platform,"Signed in as %s\n",auth.user.display_name.c_str());}
        for(;;){
            try{auth=refresh_auth_resilient(gfn,auth);save_session(auth,kSessionFile);}catch(const std::exception&e){ON_LOGW("auth","pre-catalog renewal failed: %s",e.what());}
            ON_LOGI("catalog","opening fast catalog browser");platform.clear_text();ui_printf(platform,"Loading GeForce NOW game list...\n");GameInfo game=select_game(gfn,auth,platform);save_session(auth,kSessionFile);opennow::StreamConfig cfg;platform.clear_text();ui_printf(platform,"Launching %s at %dx%d@%d, %d kbps...\n",game.title.c_str(),cfg.width,cfg.height,cfg.fps,cfg.bitrate_kbps);ON_LOGI("catalog","selected title=%s store=%s app_id_length=%u",game.title.c_str(),game.store.c_str(),(unsigned)game.launch_app_id.size());ON_LOGI("session","starting CloudMatch resolution=%dx%d fps=%d bitrate_kbps=%d",cfg.width,cfg.height,cfg.fps,cfg.bitrate_kbps);platform.clear_text();ui_printf(platform,"NOW LOADING: %s\n\n[1/4] Requesting a cloud rig...\nContacting GeForce NOW.\n",game.title.c_str());
            opennow::SessionInfo sess=gfn.start_session(auth,game,cfg);if(sess.session_id.empty())throw std::runtime_error("CloudMatch returned no sessionId");int unknown_status_polls=0,transient_poll_errors=0;for(int i=0;i<240;++i){if(sess.status>=2&&!sess.signaling_url.empty())break;if(sess.status>3&&sess.status!=6)throw std::runtime_error("Session ended or was aborted by NVIDIA");if(sess.status<0){if(++unknown_status_polls>=6)throw std::runtime_error("NVIDIA returned an unknown session status for too long");}else unknown_status_polls=0;const char spinner[]={'|','/','-','\\'};platform.clear_text();ui_printf(platform,"NOW LOADING: %s\n\n",game.title.c_str());if(sess.app_patching)ui_printf(platform,"[2/4] Updating the game %c\n",spinner[i&3]);else if(sess.status==0&&sess.queue_position>0)ui_printf(platform,"[2/4] Waiting for a cloud rig %c\nPosition in queue: %d\n",spinner[i&3],sess.queue_position);else ui_printf(platform,"[2/4] Preparing your cloud rig %c\n",spinner[i&3]);ui_printf(platform,"\nElapsed: %d seconds\n",i*5);platform.poll();for(int wait=0;wait<50;++wait){platform.poll();sleep_ms(100);}try{sess=gfn.poll_session(auth,sess.session_id);transient_poll_errors=0;}catch(const std::exception&e){if(++transient_poll_errors>3)throw;ON_LOGW("session","transient poll failure attempt=%d error=%s",transient_poll_errors,e.what());}if(sess.status==4)throw std::runtime_error("GFN session failed/closed before streaming");}
            if(sess.signaling_url.empty())throw std::runtime_error("CloudMatch never supplied a signaling URL");platform.clear_text();ui_printf(platform,"NOW LOADING: %s\n\n[3/4] Connecting to the streaming server...\n",game.title.c_str());opennow::WebRtcSession stream(sess,cfg,platform);if(!stream.start())throw std::runtime_error("WebRTC start: "+stream.state());ON_LOGI("stream","WebRTC start returned success state=%s",stream.state().c_str());platform.clear_text();ui_printf(platform,"WebRTC started. Waiting for video...\nLB+RB+Y opens the OpenNOW stream menu.\n");
            int log_tick=0,loading_tick=0,menu_index=0,keyboard_index=0,mouse_tick=0;bool stream_menu=false,mouse_mode=false,keyboard_active=false,end_requested=false,menu_dirty=false,suppress_until_neutral=false,mouse_left_down=false,mouse_right_down=false,menu_trigger_latched=false;std::string keyboard_text;std::uint16_t previous_buttons=0;DWORD next_auth_check=GetTickCount()+60000;const char*menu_items[5]={"RESUME","INPUT MODE","OPEN KEYBOARD","OPEN GFN OVERLAY - GUIDE","RETURN TO LIBRARY - END SESSION"};
            for(;stream.running();){platform.poll();stream.poll();opennow::GamepadState pad;if(platform.read_gamepad(pad)){const std::uint16_t pressed=(std::uint16_t)(pad.buttons&~previous_buttons);const std::uint16_t menu_mask=(std::uint16_t)(0x0100|0x0200|0x8000);const bool menu_chord=(pad.buttons&menu_mask)==menu_mask;if(!menu_chord)menu_trigger_latched=false;const bool menu_started=menu_chord&&!menu_trigger_latched;
                    if(menu_started&&!keyboard_active){menu_trigger_latched=true;if(mouse_left_down){stream.send_mouse_button(1,false);mouse_left_down=false;}if(mouse_right_down){stream.send_mouse_button(3,false);mouse_right_down=false;}stream_menu=!stream_menu;menu_index=0;menu_dirty=stream_menu;suppress_until_neutral=true;opennow::GamepadState neutral;stream.send_gamepad(neutral);ON_LOGI("stream-ui","stream menu %s trigger=LB+RB+Y mode=%s",stream_menu?"opened":"closed",mouse_mode?"mouse":"gamepad");if(!stream_menu)platform.set_stream_overlay(NULL);}
                    if(stream_menu){if(pressed&1){menu_index=(menu_index+4)%5;menu_dirty=true;}if(pressed&2){menu_index=(menu_index+1)%5;menu_dirty=true;}if(pressed&0x2000){stream_menu=false;suppress_until_neutral=true;}if(pressed&0x1000){if(menu_index==0){stream_menu=false;suppress_until_neutral=true;}else if(menu_index==1){mouse_mode=!mouse_mode;menu_dirty=true;suppress_until_neutral=true;opennow::GamepadState neutral;stream.send_gamepad(neutral);}else if(menu_index==2){keyboard_active=true;stream_menu=false;keyboard_index=0;keyboard_text.clear();suppress_until_neutral=true;render_local_keyboard(platform,keyboard_index,keyboard_text);}else if(menu_index==3){opennow::GamepadState guide,neutral;guide.buttons=0x0400;stream.send_gamepad(guide);stream.send_gamepad(neutral);stream_menu=false;suppress_until_neutral=true;}else end_requested=true;}if(stream_menu&&menu_dirty){char menu[1024];int used=_snprintf(menu,sizeof(menu),"OPENNOW STREAM MENU\n\n");for(int mi=0;mi<5&&used>0&&used<(int)sizeof(menu);++mi)used+=_snprintf(menu+used,sizeof(menu)-used,"%c %s%s\n",mi==menu_index?'>':' ',menu_items[mi],mi==1?(mouse_mode?": MOUSE":": GAMEPAD"):"");int off=std::min<int>(used,(int)sizeof(menu)-1);_snprintf(menu+off,sizeof(menu)-off,"\nUP DOWN: SELECT    A: CONFIRM    B: RESUME\nLB+RB+Y: OPEN OR CLOSE MENU\nBACK AND START ALWAYS PASS THROUGH TO THE GAME");menu[sizeof(menu)-1]=0;platform.set_stream_overlay(menu);menu_dirty=false;}else if(!stream_menu&&!keyboard_active)platform.set_stream_overlay(NULL);}
                    else if(keyboard_active){bool dirty=false;if(pressed&4){if(keyboard_index<kKeyboardCharCount){int row=keyboard_index/6,col=keyboard_index%6;keyboard_index=row*6+(col+5)%6;}else keyboard_index=kKeyboardBackspace+(keyboard_index-kKeyboardBackspace+2)%3;dirty=true;}if(pressed&8){if(keyboard_index<kKeyboardCharCount){int row=keyboard_index/6,col=keyboard_index%6;keyboard_index=row*6+(col+1)%6;}else keyboard_index=kKeyboardBackspace+(keyboard_index-kKeyboardBackspace+1)%3;dirty=true;}if(pressed&1){if(keyboard_index<kKeyboardCharCount){if(keyboard_index>=6)keyboard_index-=6;}else keyboard_index=36+(keyboard_index-kKeyboardBackspace)*2;dirty=true;}if(pressed&2){if(keyboard_index<36)keyboard_index+=6;else if(keyboard_index<kKeyboardCharCount)keyboard_index=kKeyboardBackspace+std::min(2,(keyboard_index-36)/2);dirty=true;}if(pressed&0x2000){if(!keyboard_text.empty())keyboard_text.erase(keyboard_text.size()-1);dirty=true;}if(pressed&0x8000){keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;keyboard_text.clear();}if(pressed&0x1000){if(keyboard_index<kKeyboardCharCount){if(keyboard_text.size()<64)keyboard_text.push_back(kKeyboardChars[keyboard_index]);dirty=true;}else if(keyboard_index==kKeyboardBackspace){if(!keyboard_text.empty())keyboard_text.erase(keyboard_text.size()-1);dirty=true;}else if(keyboard_index==kKeyboardSend){if(!keyboard_text.empty())stream.send_text(keyboard_text,true);keyboard_text.clear();keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;}else{keyboard_text.clear();keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;}}if((pressed&0x0010)&&keyboard_active){if(!keyboard_text.empty())stream.send_text(keyboard_text,true);keyboard_text.clear();keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;}if(keyboard_active&&dirty)render_local_keyboard(platform,keyboard_index,keyboard_text);}
                    else if(mouse_mode){if(pressed&0x8000){if(mouse_left_down)stream.send_mouse_button(1,false);if(mouse_right_down)stream.send_mouse_button(3,false);mouse_left_down=mouse_right_down=false;mouse_mode=false;suppress_until_neutral=true;opennow::GamepadState neutral;stream.send_gamepad(neutral);}else if(suppress_until_neutral){const bool active=pad.buttons||pad.left_trigger||pad.right_trigger||pad.lx>0.15f||pad.lx<-0.15f||pad.ly>0.15f||pad.ly<-0.15f||pad.rx>0.15f||pad.rx<-0.15f||pad.ry>0.15f||pad.ry<-0.15f;if(!active)suppress_until_neutral=false;}else{if(++mouse_tick>=16){mouse_tick=0;float ax=pad.lx,ay=pad.ly;if(ax>-0.16f&&ax<0.16f)ax=0;if(ay>-0.16f&&ay<0.16f)ay=0;std::int16_t dx=(std::int16_t)(ax*24.0f),dy=(std::int16_t)(ay*24.0f);if(dx||dy)stream.send_mouse_move(dx,dy);}if(pressed&0x1000){stream.send_mouse_button(1,true);mouse_left_down=true;}if(mouse_left_down&&!(pad.buttons&0x1000)){stream.send_mouse_button(1,false);mouse_left_down=false;}if(pressed&0x2000){stream.send_mouse_button(3,true);mouse_right_down=true;}if(mouse_right_down&&!(pad.buttons&0x2000)){stream.send_mouse_button(3,false);mouse_right_down=false;}if(pressed&1)stream.send_mouse_wheel(120);if(pressed&2)stream.send_mouse_wheel(-120);}}
                    else if(!menu_chord){const bool active=pad.buttons||pad.left_trigger||pad.right_trigger||pad.lx>0.15f||pad.lx<-0.15f||pad.ly>0.15f||pad.ly<-0.15f||pad.rx>0.15f||pad.rx<-0.15f||pad.ry>0.15f||pad.ry<-0.15f;if(suppress_until_neutral){if(!active){suppress_until_neutral=false;opennow::GamepadState neutral;stream.send_gamepad(neutral);}}else{opennow::GamepadState gamepad=pad;gamepad.ly=-gamepad.ly;gamepad.ry=-gamepad.ry;stream.send_gamepad(gamepad);}}
                    previous_buttons=pad.buttons;}
                if(end_requested)break;if((LONG)(GetTickCount()-next_auth_check)>=0){next_auth_check=GetTickCount()+60000;if(auth_near_expiry(auth)){try{auth=refresh_auth_resilient(gfn,auth);save_session(auth,kSessionFile);ON_LOGI("auth","in-session token renewal persisted");}catch(const std::exception&e){ON_LOGW("auth","in-session token renewal failed while media continues: %s",e.what());}}}if(++log_tick>=3000){log_tick=0;ON_LOGI("stream","periodic state=%s rtt_ms=%d",stream.state().c_str(),stream.rtt_ms());}if(!stream.has_video()&&++loading_tick>=1000){loading_tick=0;platform.clear_text();ui_printf(platform,"NOW LOADING: %s\n\n[3/4] Establishing media...\nState: %s\nRTT: %d ms\n\nLB+RB+Y: stream menu\n",game.title.c_str(),stream.state().c_str(),stream.rtt_ms());}sleep_ms(1);}
            const std::string final_stream_state=stream.state();platform.set_stream_overlay(NULL);stream.stop();gfn.stop_session(auth,sess.session_id);try{auth=refresh_auth_resilient(gfn,auth);save_session(auth,kSessionFile);}catch(const std::exception&e){ON_LOGW("auth","post-session renewal failed: %s",e.what());}platform.clear_text();if(final_stream_state=="failed"||final_stream_state=="disconnected")ui_printf(platform,"Streaming connection failed.\nState: %s\n\nSee %s for diagnostics.\n",final_stream_state.c_str(),opennow::log_path());else ui_printf(platform,"Session ended.\nState: %s\n",final_stream_state.c_str());sleep_ms(700);
        }
    }catch(const std::exception&e){ON_LOGF("main","unhandled exception: %s",e.what());platform.clear_text();ui_printf(platform,"FATAL:\n%s\n\nSee %s for diagnostics.\n",e.what(),opennow::log_path());}
    ui_printf(platform,"\nPress controller A to exit.\n");for(;;){platform.poll();opennow::GamepadState s;if(platform.read_gamepad(s)&&(s.buttons&0x1000))break;sleep_ms(20);}opennow::log_close();return 0;
}
#else
int main(){std::puts("OpenNOW Xbox 360 target requires LibXenon or OPENNOW_XDK.");return 0;}
#endif
