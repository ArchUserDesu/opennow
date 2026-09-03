#include "opennow/gfn_client.hpp"
#include "opennow/persistence.hpp"
#include "opennow/webrtc_session.hpp"
#include "opennow/xenon_platform.hpp"
#include "opennow/logger.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
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

void sleep_ms(int ms) {
#ifdef __LIBXENON__
    mdelay(ms);
#elif defined(OPENNOW_XDK)
    Sleep((DWORD)ms);
#else
    (void)ms;
#endif
}

void ui_printf(XenonPlatform& platform, const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    #ifdef OPENNOW_XDK
    _vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    buffer[sizeof(buffer)-1] = '\0';
#else
    std::vsnprintf(buffer, sizeof(buffer), format, args);
#endif
    va_end(args);
    platform.write_text(buffer);
}

void wait_release(XenonPlatform& p) {
    GamepadState s;
    for (int i = 0; i < 50; ++i) {
        p.poll();
        if (!p.read_gamepad(s) || s.buttons == 0) return;
        sleep_ms(20);
    }
}

int choose(XenonPlatform& p, const std::vector<std::string>& labels, const char* title) {
    if (labels.empty()) return -1;
    int idx = 0;
    int last = -1;
    for (;;) {
        if (idx != last) {
            p.clear_text();
            ui_printf(p, "=== %s ===\n\n", title);
            const int first = (idx / 10) * 10;
            for (int i = first; i < std::min<int>(first + 10, labels.size()); ++i) {
                ui_printf(p, "%c %d. %s\n", i == idx ? '>' : ' ', i + 1, labels[i].c_str());
            }
            ui_printf(p, "\nD-pad: select   A: confirm   B: cancel\n");
            last = idx;
        }
        p.poll();
        GamepadState g;
        if (p.read_gamepad(g)) {
            if (g.buttons & 0x0001) {
                idx = (idx - 1 + (int)labels.size()) % (int)labels.size();
                wait_release(p);
            } else if (g.buttons & 0x0002) {
                idx = (idx + 1) % (int)labels.size();
                wait_release(p);
            } else if (g.buttons & 0x1000) {
                wait_release(p);
                return idx;
            } else if (g.buttons & 0x2000) {
                wait_release(p);
                return -1;
            }
        }
        sleep_ms(20);
    }
}

LoginProvider select_provider(GfnClient& g, XenonPlatform& p) {
    std::vector<LoginProvider> providers = g.fetch_login_providers();
    if (providers.empty()) throw std::runtime_error("No GFN login providers returned");
    std::vector<std::string> names;
    for (std::size_t i = 0; i < providers.size(); ++i) {
        const LoginProvider& provider = providers[i];
        names.push_back(provider.display_name + " [" + provider.code + "]");
    }
    const int n = choose(p, names, "GeForce NOW provider");
    if (n < 0) throw std::runtime_error("provider selection cancelled");
    return providers[n];
}

GameInfo select_game(GfnClient& g, AuthSession& auth, XenonPlatform& p) {
    p.clear_text();
    ui_printf(p, "Fetching GFN catalog...\n");
    std::vector<GameInfo> games = g.fetch_catalog_games(auth);
    if (games.empty()) throw std::runtime_error("GFN authenticated catalog is empty");

    struct GameSort {
        bool operator()(const GameInfo& a, const GameInfo& b) const {
            const bool am = a.title.find("Marvel Rivals") != std::string::npos;
            const bool bm = b.title.find("Marvel Rivals") != std::string::npos;
            if (am != bm) return am;
            return a.title < b.title;
        }
    };
    std::stable_sort(games.begin(), games.end(), GameSort());

    std::vector<GameInfo> library;
    std::vector<std::string> names, library_names;
    names.reserve(games.size()); library.reserve(games.size()); library_names.reserve(games.size());
    for (std::size_t i = 0; i < games.size(); ++i) {
        const GameInfo& game = games[i];
        names.push_back(game.title + (game.store.empty() ? "" : " [" + game.store + "]"));
        if (game.in_library) {
            library.push_back(game);
            library_names.push_back(names.back());
        }
    }
    ON_LOGI("catalog", "catalog tabs prepared all=%u library=%u", (unsigned)games.size(), (unsigned)library.size());

    bool library_tab = false;
    int indices[2] = {0, 0};
    GameInfo game;
    bool selected = false;
    for (;;) {
        const std::vector<GameInfo>& tab_games = library_tab ? library : games;
        const std::vector<std::string>& tab_names = library_tab ? library_names : names;
        int& idx = indices[library_tab ? 1 : 0];
        if (!tab_games.empty() && idx >= (int)tab_games.size()) idx = (int)tab_games.size()-1;
        p.clear_text();
        ui_printf(p, "LB/RB  [ALL GAMES]%s  [MY LIBRARY]%s\n",
                  library_tab ? "" : " <", library_tab ? " <" : "");
        ui_printf(p, "%s: %u games   Page %u/%u\n",
                  library_tab ? "My Library" : "All Games", (unsigned)tab_games.size(),
                  tab_games.empty()?0u:(unsigned)(idx/20+1),
                  tab_games.empty()?0u:(unsigned)((tab_games.size()+19)/20));
        if (tab_games.empty()) ui_printf(p, "Your GeForce NOW library is empty.\nUse LB to return to All Games.\n");
        else {
            const int first=(idx/20)*20;
            for(int i=first;i<std::min<int>(first+20,tab_names.size());++i)
                ui_printf(p,"%c %d. %s\n",i==idx?'>':' ',i+1,tab_names[i].c_str());
        }
        ui_printf(p,"UP/DOWN: select  LEFT/RIGHT: skip 20  A: play  B: cancel\n");
        bool redraw=false;
        for (;;) {
            p.poll(); GamepadState pad;
            if(p.read_gamepad(pad)) {
                if((pad.buttons&0x0100)||(pad.buttons&0x0200)) { library_tab=!library_tab; ON_LOGI("catalog","tab changed tab=%s",library_tab?"library":"all"); wait_release(p); redraw=true; break; }
                if(!tab_games.empty()&&(pad.buttons&0x0001)){idx=(idx-1+(int)tab_games.size())%(int)tab_games.size();wait_release(p);redraw=true;break;}
                if(!tab_games.empty()&&(pad.buttons&0x0002)){idx=(idx+1)%(int)tab_games.size();wait_release(p);redraw=true;break;}
                if(!tab_games.empty()&&(pad.buttons&0x0004)){const int count=(int)tab_games.size();idx=(idx-(20%count)+count)%count;wait_release(p);redraw=true;break;}
                if(!tab_games.empty()&&(pad.buttons&0x0008)){idx=(idx+20)%(int)tab_games.size();wait_release(p);redraw=true;break;}
                if(!tab_games.empty()&&(pad.buttons&0x1000)){game=tab_games[idx];wait_release(p);selected=true;break;}
                if(pad.buttons&0x2000){wait_release(p);throw std::runtime_error("game selection cancelled");}
            }
            sleep_ms(20);
        }
        if(selected) break;
        if(redraw) continue;
    }

    if (game.variants.size() > 1) {
        std::vector<std::string> variants;
        for (std::size_t i = 0; i < game.variants.size(); ++i) {
            const GameVariant& v = game.variants[i];
            variants.push_back((v.store.empty() ? "variant" : v.store) + "  id=" + v.id);
        }
        const int vi = choose(p, variants, "Select store/variant");
        if (vi >= 0) {
            game.launch_app_id = game.variants[vi].id;
            game.store = game.variants[vi].store;
            game.internal_title = game.variants[vi].internal_title;
        }
    }
    if (game.launch_app_id.empty()) throw std::runtime_error("Selected game has no launch app id");
    return game;
}

const char* const kKeyboardChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@.-_/ ";
const int kKeyboardCharCount = 42;
const int kKeyboardBackspace = 42;
const int kKeyboardSend = 43;
const int kKeyboardCancel = 44;

void render_local_keyboard(XenonPlatform& platform, int selected, const std::string& entered) {
    char overlay[1536];
    int used = _snprintf(overlay, sizeof(overlay), "OPENNOW KEYBOARD\n\nTEXT: %s\n\n", entered.empty() ? "(EMPTY)" : entered.c_str());
    for (int row = 0; row < 7 && used > 0 && used < (int)sizeof(overlay); ++row) {
        for (int col = 0; col < 6 && used > 0 && used < (int)sizeof(overlay); ++col) {
            const int index = row * 6 + col;
            used += _snprintf(overlay + used, sizeof(overlay) - used, index == selected ? "[%c] " : " %c  ", kKeyboardChars[index]);
        }
        if (used > 0 && used < (int)sizeof(overlay)) used += _snprintf(overlay + used, sizeof(overlay) - used, "\n");
    }
    if (used > 0 && used < (int)sizeof(overlay))
        _snprintf(overlay + used, sizeof(overlay) - used, "\n%sBACKSPACE%s  %sSEND%s  %sCANCEL%s\n\nDPAD: MOVE    A: SELECT    B: ERASE\nSTART: SEND    Y: CANCEL",
                  selected == kKeyboardBackspace ? "[" : " ", selected == kKeyboardBackspace ? "]" : " ",
                  selected == kKeyboardSend ? "[" : " ", selected == kKeyboardSend ? "]" : " ",
                  selected == kKeyboardCancel ? "[" : " ", selected == kKeyboardCancel ? "]" : " ");
    overlay[sizeof(overlay)-1] = 0;
    platform.set_stream_overlay(overlay);
}
} // namespace

#if defined(__LIBXENON__) || defined(OPENNOW_XDK)
int main() {
    opennow::XenonPlatform platform;
    if (!platform.init()) {
        ON_LOGF("main", "platform initialization failed; aborting before GFN startup");
        platform.clear_text();
        ui_printf(platform, "OpenNOW: platform/network initialization failed.\nSee opennow.log for details.\n");
        opennow::log_close();
        sleep_ms(5000);
        return 1;
    }
    if(!opennow::input_encoding_self_test()){
        ON_LOGF("input","GFN input packet encoding self-test failed");
        platform.clear_text();ui_printf(platform,"OpenNOW input protocol self-test failed.\nSee opennow.log.\n");opennow::log_close();return 1;
    }
    ON_LOGI("input","GFN gamepad mouse keyboard packet encoding self-test passed");

    ON_LOGI("main", "entered application main; persistent_log=%s", opennow::log_path());
    platform.clear_text();
    ui_printf(platform,
#ifdef OPENNOW_XDK
              "OpenNOW Xbox 360 XEX - CPU H.264 build\n"
              "Running under the Xbox kernel; console Wi-Fi/network configuration is used.\n\n");
#else
              "OpenNOW-Xenon CPU H.264 build\n"
              "A full GFN session is driven natively; no browser is used.\n\n");
#endif

    opennow::GfnClient gfn;
    opennow::AuthSession auth;
    try {
        ON_LOGI("main", "checking saved authentication session at %s", kSessionFile);
        if (load_session(auth, kSessionFile)) {
            ON_LOGI("auth", "saved session loaded provider=%s user_name_length=%u",
                    auth.provider.code.c_str(), (unsigned)auth.user.display_name.size());
            ui_printf(platform, "Loaded saved GFN login for %s\n", auth.user.display_name.c_str());
            try {
                ON_LOGI("auth", "refreshing saved session if required");
                auth = gfn.refresh(auth);
                const bool saved = save_session(auth, kSessionFile);
                ON_LOGI("auth", "saved session refresh complete persisted=%d", saved ? 1 : 0);
            } catch (const std::exception& e) {
                ON_LOGE("auth", "saved session refresh failed: %s", e.what());
                ui_printf(platform, "Saved session refresh failed: %s\nStarting QR login.\n", e.what());
                auth = opennow::AuthSession();
            }
        } else {
            ON_LOGI("auth", "no usable saved session found");
        }

        if (auth.tokens.access_token.empty()) {
            ON_LOGI("auth", "fetching login providers");
            LoginProvider provider = select_provider(gfn, platform);
            ON_LOGI("auth", "selected login provider code=%s", provider.code.c_str());
            auth = gfn.login_qr(
                provider,
                [&](const opennow::QrLoginChallenge& q) {
                    ON_LOGI("auth", "QR challenge received interval=%d expires_at_ms=%lld",
                            q.interval_seconds, (long long)q.expires_at_ms);
                    platform.clear_text();
                    ui_printf(platform,
                              "SIGN IN TO GEFORCE NOW\n\n"
                              "Open: %s\n\nCode: %s\n\n"
                              "Direct URL:\n%s\n\nWaiting for authorization...\n"
                              "B: cancel\n",
                              q.verification_uri.c_str(), q.user_code.c_str(),
                              q.verification_uri_complete.c_str());
                },
                [&]() -> bool {
                    platform.poll();
                    opennow::GamepadState s;
                    return platform.read_gamepad(s) && (s.buttons & 0x2000);
                });
            const bool saved = save_session(auth, kSessionFile);
            ON_LOGI("auth", "QR login complete session_persisted=%d", saved ? 1 : 0);
            platform.clear_text();
            ui_printf(platform, "Signed in as %s\n", auth.user.display_name.c_str());
        }

        for (;;) {
        ON_LOGI("catalog", "fetching authenticated catalog");
        platform.clear_text();
        ui_printf(platform, "Loading the complete GeForce NOW catalog...\nFetching every NVIDIA catalog page.\n");
        GameInfo game = select_game(gfn, auth, platform);
        opennow::StreamConfig cfg;
        platform.clear_text();
        ui_printf(platform, "Launching %s at %dx%d@%d, %d kbps...\n",
                  game.title.c_str(), cfg.width, cfg.height, cfg.fps, cfg.bitrate_kbps);
        ON_LOGI("catalog", "selected title=%s store=%s app_id_length=%u",
                game.title.c_str(), game.store.c_str(), (unsigned)game.launch_app_id.size());
        ON_LOGI("session", "starting CloudMatch resolution=%dx%d fps=%d bitrate_kbps=%d",
                cfg.width, cfg.height, cfg.fps, cfg.bitrate_kbps);
        platform.clear_text();
        ui_printf(platform, "NOW LOADING: %s\n\n[1/4] Requesting a cloud rig...\nContacting GeForce NOW.\n",
                  game.title.c_str());

        opennow::SessionInfo sess = gfn.start_session(auth, game, cfg);
        if (sess.session_id.empty()) throw std::runtime_error("CloudMatch returned no sessionId");
        ON_LOGI("session", "CloudMatch session created id_length=%u status=%d",
                (unsigned)sess.session_id.size(), sess.status);

        int unknown_status_polls = 0;
        int transient_poll_errors = 0;
        for (int i = 0; i < 240; ++i) {
            if (sess.status >= 2 && !sess.signaling_url.empty()) break;
            if (sess.status > 3 && sess.status != 6) throw std::runtime_error("Session ended or was aborted by NVIDIA");
            if (sess.status < 0) { if (++unknown_status_polls >= 6) throw std::runtime_error("NVIDIA returned an unknown session status for too long"); }
            else unknown_status_polls = 0;
            const char spinner[] = {'|','/','-','\\'};
            platform.clear_text();
            ui_printf(platform, "NOW LOADING: %s\n\n", game.title.c_str());
            if (sess.app_patching)
                ui_printf(platform, "[2/4] Updating the game %c\nNVIDIA is patching it; launch continues automatically.\n", spinner[i&3]);
            else if (sess.status == 0 && sess.queue_position > 0)
                ui_printf(platform, "[2/4] Waiting for a cloud rig %c\nPosition in queue: %d\n", spinner[i&3], sess.queue_position);
            else if (sess.status == 0)
                ui_printf(platform, "[2/4] Waiting for an available cloud rig %c\n", spinner[i&3]);
            else if (sess.status == 6)
                ui_printf(platform, "[2/4] Waiting for NVIDIA confirmation/ads %c\n", spinner[i&3]);
            else
                ui_printf(platform, "[2/4] Preparing your cloud rig %c\nStarting the game and configuring your seat.\n", spinner[i&3]);
            ui_printf(platform, "\nElapsed: %d seconds   BACK+START: exit\n", i*5);
            ON_LOGI("session", "poll status=%d queue=%d patching=%d signaling=%d media=%d elapsed_s=%d",
                    sess.status, sess.queue_position, sess.app_patching ? 1 : 0,
                    sess.signaling_url.empty() ? 0 : 1, sess.media_ip.empty() ? 0 : 1, i*5);
            platform.poll();
            for(int wait=0;wait<50;++wait){platform.poll();sleep_ms(100);}
            try { sess = gfn.poll_session(auth, sess.session_id); transient_poll_errors=0; }
            catch(const std::exception& e) {
                if (++transient_poll_errors > 3) throw;
                ON_LOGW("session","transient poll failure attempt=%d error=%s",transient_poll_errors,e.what());
            }
            if (sess.status == 4)
                throw std::runtime_error("GFN session failed/closed before streaming");
        }

        if (sess.signaling_url.empty())
            throw std::runtime_error("CloudMatch never supplied a signaling URL");
        ON_LOGI("session", "starting WebRTC signaling_url_length=%u media_host_length=%u media_port=%d ice_servers=%u",
                (unsigned)sess.signaling_url.size(), (unsigned)sess.media_ip.size(),
                sess.media_port, (unsigned)sess.ice_servers.size());

        platform.clear_text();
        ui_printf(platform, "NOW LOADING: %s\n\n[3/4] Connecting to the streaming server...\nInitializing H.264, Opus, ICE and secure WebRTC.\n", game.title.c_str());
        opennow::WebRtcSession stream(sess, cfg, platform);
        if (!stream.start()) throw std::runtime_error("WebRTC start: " + stream.state());
        ON_LOGI("stream", "WebRTC start returned success state=%s", stream.state().c_str());
        platform.clear_text();
        ui_printf(platform, "WebRTC started. Waiting for video...\nBACK+START opens the stream menu.\n");

        int log_tick = 0;
        int loading_tick = 0;
        bool stream_menu=false,mouse_mode=false,keyboard_active=false,end_requested=false,menu_dirty=false,suppress_until_neutral=false,mouse_left_down=false,mouse_right_down=false,overlay_trigger_latched=false;
        int menu_index=0,keyboard_index=0;
        std::string keyboard_text;
        std::uint16_t previous_buttons=0;
        int mouse_tick=0;
        DWORD back_pressed_ms=0,start_pressed_ms=0;
        const char* menu_items[5]={"RESUME","INPUT MODE","OPEN KEYBOARD","OPEN GFN OVERLAY - GUIDE","RETURN TO LIBRARY - END SESSION"};
        for (; stream.running();) {
            platform.poll();
            stream.poll();
            opennow::GamepadState pad;
            if (platform.read_gamepad(pad)) {
                const std::uint16_t pressed=(std::uint16_t)(pad.buttons&~previous_buttons);
                const DWORD input_now_ms=GetTickCount();
                const bool back_down=(pad.buttons&0x0020)!=0,start_down=(pad.buttons&0x0010)!=0;
                if(pressed&0x0020){back_pressed_ms=input_now_ms;ON_LOGI("stream-ui","BACK press observed mapped=0x%04x",(unsigned)pad.buttons);}
                if(pressed&0x0010){start_pressed_ms=input_now_ms;ON_LOGI("stream-ui","START press observed mapped=0x%04x",(unsigned)pad.buttons);}
                if(!back_down&&!start_down)overlay_trigger_latched=false;
                const bool exact_chord=back_down&&start_down;
                const bool chord_window=(back_down&&start_pressed_ms&&input_now_ms-start_pressed_ms<=650)||(start_down&&back_pressed_ms&&input_now_ms-back_pressed_ms<=650);
                const bool hold_back=back_down&&back_pressed_ms&&input_now_ms-back_pressed_ms>=900;
                const bool overlay_chord=exact_chord||chord_window||hold_back;
                const bool overlay_chord_started=overlay_chord&&!overlay_trigger_latched;
                const bool gfn_chord=(pad.buttons&(0x0020|0x8000))==(0x0020|0x8000);
                const bool gfn_chord_started=gfn_chord&&((previous_buttons&(0x0020|0x8000))!=(0x0020|0x8000));
                if(overlay_chord_started&&!keyboard_active){
                    overlay_trigger_latched=true;
                    if(mouse_left_down){stream.send_mouse_button(1,false);mouse_left_down=false;}if(mouse_right_down){stream.send_mouse_button(3,false);mouse_right_down=false;}
                    stream_menu=!stream_menu;
                    menu_index=0;
                    menu_dirty=stream_menu;
                    suppress_until_neutral=true;
                    opennow::GamepadState neutral;stream.send_gamepad(neutral);
                    ON_LOGI("stream-ui","stream menu %s trigger=%s mode=%s back_age=%u start_age=%u",stream_menu?"opened":"closed",exact_chord?"exact-chord":(hold_back?"hold-back":"chord-window"),mouse_mode?"mouse":"gamepad",back_pressed_ms?(unsigned)(input_now_ms-back_pressed_ms):0,start_pressed_ms?(unsigned)(input_now_ms-start_pressed_ms):0);
                    if(!stream_menu)platform.set_stream_overlay(NULL);
                }
                if(gfn_chord_started&&!stream_menu&&!keyboard_active){opennow::GamepadState guide,neutral;guide.buttons=0x0400;stream.send_gamepad(guide);stream.send_gamepad(neutral);suppress_until_neutral=true;ON_LOGI("stream-ui","BACK+Y sent virtual Xbox Guide to GFN");}
                if(stream_menu){
                    if(pressed&0x0001){menu_index=(menu_index+4)%5;menu_dirty=true;}
                    if(pressed&0x0002){menu_index=(menu_index+1)%5;menu_dirty=true;}
                    if(pressed&0x2000){stream_menu=false;suppress_until_neutral=true;ON_LOGI("stream-ui","menu dismissed with B");}
                    if(pressed&0x1000){
                        if(menu_index==0){stream_menu=false;suppress_until_neutral=true;}
                        else if(menu_index==1){mouse_mode=!mouse_mode;menu_dirty=true;suppress_until_neutral=true;opennow::GamepadState neutral;stream.send_gamepad(neutral);ON_LOGI("stream-ui","input mode changed mode=%s",mouse_mode?"mouse":"gamepad");}
                        else if(menu_index==2){keyboard_active=true;stream_menu=false;keyboard_index=0;keyboard_text.clear();suppress_until_neutral=true;render_local_keyboard(platform,keyboard_index,keyboard_text);ON_LOGI("stream-ui","in-app keyboard opened");}
                        else if(menu_index==3){opennow::GamepadState guide,neutral;guide.buttons=0x0400;stream.send_gamepad(guide);stream.send_gamepad(neutral);stream_menu=false;suppress_until_neutral=true;ON_LOGI("stream-ui","menu sent virtual Xbox Guide to GFN");}
                        else {end_requested=true;ON_LOGI("stream-ui","return to library selected");}
                    }
                    if(stream_menu&&menu_dirty){
                        char menu[1024];int used=_snprintf(menu,sizeof(menu),"OPENNOW STREAM MENU\n\n");
                        for(int mi=0;mi<5&&used>0&&used<(int)sizeof(menu);++mi)used+=_snprintf(menu+used,sizeof(menu)-used,"%c %s%s\n",mi==menu_index?'>':' ',menu_items[mi],mi==1?(mouse_mode?": MOUSE":": GAMEPAD"):"");
                        const int offset=std::min<int>(used,(int)sizeof(menu)-1);_snprintf(menu+offset,sizeof(menu)-offset,"\nUP DOWN: SELECT    A: CONFIRM    B: RESUME\nBACK+START: OPEN OR CLOSE MENU    BACK+Y: GFN GUIDE\n\nGAMEPAD MODE: CONTROLLER IS SENT AS XBOX 360 PAD\nMOUSE MODE: LEFT STICK MOVE, A LEFT CLICK, B RIGHT CLICK\nDPAD UP DOWN: SCROLL    Y: RETURN TO GAMEPAD MODE");menu[sizeof(menu)-1]=0;
                        platform.set_stream_overlay(menu);
                        menu_dirty=false;
                        if(pressed&0x0001)ON_LOGI("stream-ui","menu rendered direction=up index=%d item=%s",menu_index,menu_items[menu_index]);
                        else if(pressed&0x0002)ON_LOGI("stream-ui","menu rendered direction=down index=%d item=%s",menu_index,menu_items[menu_index]);
                    }else if(!stream_menu&&!keyboard_active)platform.set_stream_overlay(NULL);
                }else if(keyboard_active){
                    bool keyboard_dirty=false;
                    const char* keyboard_direction=NULL;
                    if(pressed&0x0004){
                        if(keyboard_index<kKeyboardCharCount){const int row=keyboard_index/6,col=keyboard_index%6;keyboard_index=row*6+(col+5)%6;}
                        else keyboard_index=kKeyboardBackspace+(keyboard_index-kKeyboardBackspace+2)%3;
                        keyboard_dirty=true;keyboard_direction="left";
                    }
                    if(pressed&0x0008){
                        if(keyboard_index<kKeyboardCharCount){const int row=keyboard_index/6,col=keyboard_index%6;keyboard_index=row*6+(col+1)%6;}
                        else keyboard_index=kKeyboardBackspace+(keyboard_index-kKeyboardBackspace+1)%3;
                        keyboard_dirty=true;keyboard_direction="right";
                    }
                    if(pressed&0x0001){
                        if(keyboard_index<kKeyboardCharCount){if(keyboard_index>=6)keyboard_index-=6;}
                        else keyboard_index=36+(keyboard_index-kKeyboardBackspace)*2;
                        keyboard_dirty=true;keyboard_direction="up";
                    }
                    if(pressed&0x0002){
                        if(keyboard_index<36)keyboard_index+=6;
                        else if(keyboard_index<kKeyboardCharCount)keyboard_index=kKeyboardBackspace+std::min(2,(keyboard_index-36)/2);
                        keyboard_dirty=true;keyboard_direction="down";
                    }
                    if(pressed&0x2000){if(!keyboard_text.empty())keyboard_text.erase(keyboard_text.size()-1);keyboard_dirty=true;ON_LOGI("stream-ui","keyboard erase chars_now=%u",(unsigned)keyboard_text.size());}
                    if(pressed&0x8000){keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;keyboard_text.clear();ON_LOGI("stream-ui","in-app keyboard cancelled");}
                    if(pressed&0x1000){
                        if(keyboard_index<kKeyboardCharCount){if(keyboard_text.size()<64)keyboard_text.push_back(kKeyboardChars[keyboard_index]);keyboard_dirty=true;ON_LOGI("stream-ui","keyboard character entered index=%d chars_now=%u",keyboard_index,(unsigned)keyboard_text.size());}
                        else if(keyboard_index==kKeyboardBackspace){if(!keyboard_text.empty())keyboard_text.erase(keyboard_text.size()-1);keyboard_dirty=true;ON_LOGI("stream-ui","keyboard backspace selected chars_now=%u",(unsigned)keyboard_text.size());}
                        else if(keyboard_index==kKeyboardSend){if(!keyboard_text.empty())stream.send_text(keyboard_text,true);ON_LOGI("stream-ui","keyboard text sent chars=%u",(unsigned)keyboard_text.size());keyboard_text.clear();keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;}
                        else {keyboard_text.clear();keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;ON_LOGI("stream-ui","in-app keyboard cancelled by menu action");}
                    }
                    if((pressed&0x0010)&&keyboard_active){if(!keyboard_text.empty())stream.send_text(keyboard_text,true);ON_LOGI("stream-ui","keyboard text sent with START chars=%u",(unsigned)keyboard_text.size());keyboard_text.clear();keyboard_active=false;stream_menu=true;menu_index=2;menu_dirty=true;suppress_until_neutral=true;}
                    if(keyboard_active&&keyboard_dirty)render_local_keyboard(platform,keyboard_index,keyboard_text);
                    if(keyboard_direction)ON_LOGI("stream-ui","keyboard rendered direction=%s index=%d",keyboard_direction,keyboard_index);
                }else if(gfn_chord){
                    /* Consume both chord halves so the game never sees them. */
                }else if(mouse_mode){
                    if(pressed&0x8000){if(mouse_left_down)stream.send_mouse_button(1,false);if(mouse_right_down)stream.send_mouse_button(3,false);mouse_left_down=mouse_right_down=false;mouse_mode=false;suppress_until_neutral=true;opennow::GamepadState neutral;stream.send_gamepad(neutral);ON_LOGI("stream-ui","mouse mode exited with Y");}
                    else if(suppress_until_neutral){const bool active=pad.buttons||pad.left_trigger||pad.right_trigger||pad.lx>0.15f||pad.lx<-0.15f||pad.ly>0.15f||pad.ly<-0.15f||pad.rx>0.15f||pad.rx<-0.15f||pad.ry>0.15f||pad.ry<-0.15f;if(!active)suppress_until_neutral=false;}
                    else {
                        if(++mouse_tick>=16){mouse_tick=0;float ax=pad.lx,ay=pad.ly;if(ax>-0.16f&&ax<0.16f)ax=0;if(ay>-0.16f&&ay<0.16f)ay=0;const std::int16_t dx=(std::int16_t)(ax*24.0f),dy=(std::int16_t)(ay*24.0f);if(dx||dy)stream.send_mouse_move(dx,dy);}
                        if(pressed&0x1000){stream.send_mouse_button(1,true);mouse_left_down=true;}if(mouse_left_down&&!(pad.buttons&0x1000)){stream.send_mouse_button(1,false);mouse_left_down=false;}
                        if(pressed&0x2000){stream.send_mouse_button(3,true);mouse_right_down=true;}if(mouse_right_down&&!(pad.buttons&0x2000)){stream.send_mouse_button(3,false);mouse_right_down=false;}
                        if(pressed&0x0001)stream.send_mouse_wheel(120);if(pressed&0x0002)stream.send_mouse_wheel(-120);
                    }
                }else if(!overlay_chord){
                    const bool active=pad.buttons||pad.left_trigger||pad.right_trigger||pad.lx>0.15f||pad.lx<-0.15f||pad.ly>0.15f||pad.ly<-0.15f||pad.rx>0.15f||pad.rx<-0.15f||pad.ry>0.15f||pad.ry<-0.15f;
                    if(suppress_until_neutral){if(!active){suppress_until_neutral=false;opennow::GamepadState neutral;stream.send_gamepad(neutral);}}
                    else stream.send_gamepad(pad);
                }
                previous_buttons=pad.buttons;
            }
            if(end_requested)break;
            if (++log_tick >= 3000) {
                log_tick = 0;
                ON_LOGI("stream", "periodic state=%s rtt_ms=%d",
                        stream.state().c_str(), stream.rtt_ms());
            }
            if (!stream.has_video() && ++loading_tick >= 1000) {
                loading_tick = 0;
                platform.clear_text();
                ui_printf(platform, "NOW LOADING: %s\n\n[3/4] Establishing the media connection...\nState: %s\nRTT: %d ms\n\nBACK+START: stream menu\n",
                          game.title.c_str(), stream.state().c_str(), stream.rtt_ms());
            }
            sleep_ms(1);
        }

        const std::string final_stream_state = stream.state();
        platform.set_stream_overlay(NULL);
        ON_LOGI("stream", "stopping media and CloudMatch session");
        stream.stop();
        gfn.stop_session(auth, sess.session_id);
        ON_LOGI("session", "session shutdown complete");
        platform.clear_text();
        if (final_stream_state == "failed" || final_stream_state == "disconnected")
            ui_printf(platform, "Streaming connection failed.\nState: %s\n\nSee %s for detailed ICE/media diagnostics.\n", final_stream_state.c_str(), opennow::log_path());
        else
            ui_printf(platform, "Session ended.\nState: %s\n", final_stream_state.c_str());
        sleep_ms(700);
        }
    } catch (const std::exception& e) {
        ON_LOGF("main", "unhandled exception: %s", e.what());
        platform.clear_text();
        ui_printf(platform, "FATAL:\n%s\n\nSee %s for diagnostics.\n", e.what(), opennow::log_path());
    }

    ui_printf(platform, "\nPress controller A to exit.\n");
    ON_LOGI("main", "waiting for controller A before returning");
    for (;;) {
        platform.poll();
        opennow::GamepadState s;
        if (platform.read_gamepad(s) && (s.buttons & 0x1000)) break;
        sleep_ms(20);
    }
    ON_LOGI("main", "controller A received; application returning");
    opennow::log_close();
    return 0;
}
#else
int main() {
    std::puts("OpenNOW Xbox 360 target requires LibXenon or OPENNOW_XDK.");
    return 0;
}
#endif
