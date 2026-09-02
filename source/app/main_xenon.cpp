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

    std::vector<std::string> names;
    names.reserve(games.size());
    for (std::size_t i = 0; i < games.size(); ++i) {
        const GameInfo& game = games[i];
        names.push_back(game.title + (game.store.empty() ? "" : " [" + game.store + "]"));
    }

    const int n = choose(p, names, "Select game (Marvel Rivals is promoted to top)");
    if (n < 0) throw std::runtime_error("game selection cancelled");
    GameInfo game = games[n];

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
                [&]() {
                    platform.poll();
                    opennow::GamepadState s;
                    return platform.read_gamepad(s) && (s.buttons & 0x2000);
                });
            const bool saved = save_session(auth, kSessionFile);
            ON_LOGI("auth", "QR login complete session_persisted=%d", saved ? 1 : 0);
            platform.clear_text();
            ui_printf(platform, "Signed in as %s\n", auth.user.display_name.c_str());
        }

        ON_LOGI("catalog", "fetching authenticated catalog");
        GameInfo game = select_game(gfn, auth, platform);
        opennow::StreamConfig cfg;
        platform.clear_text();
        ui_printf(platform, "Launching %s at %dx%d@%d, %d kbps...\n",
                  game.title.c_str(), cfg.width, cfg.height, cfg.fps, cfg.bitrate_kbps);
        ON_LOGI("catalog", "selected title=%s store=%s app_id_length=%u",
                game.title.c_str(), game.store.c_str(), (unsigned)game.launch_app_id.size());
        ON_LOGI("session", "starting CloudMatch resolution=%dx%d fps=%d bitrate_kbps=%d",
                cfg.width, cfg.height, cfg.fps, cfg.bitrate_kbps);

        opennow::SessionInfo sess = gfn.start_session(auth, game, cfg);
        if (sess.session_id.empty()) throw std::runtime_error("CloudMatch returned no sessionId");
        ON_LOGI("session", "CloudMatch session created id_length=%u status=%d",
                (unsigned)sess.session_id.size(), sess.status);

        for (int i = 0; i < 3600; ++i) {
            if (sess.status >= 2 && !sess.signaling_url.empty()) break;
            if (i == 0 || i % 2 == 0) {
                platform.clear_text();
                ui_printf(platform, "Preparing %s\n\nGFN session status=%d\nQueue=%d\nPatching=%s\n",
                          game.title.c_str(), sess.status, sess.queue_position,
                          sess.app_patching ? "yes" : "no");
            }
            if (i == 0 || i % 10 == 0)
                ON_LOGI("session", "poll status=%d queue=%d patching=%d signaling=%d media=%d",
                        sess.status, sess.queue_position, sess.app_patching ? 1 : 0,
                        sess.signaling_url.empty() ? 0 : 1, sess.media_ip.empty() ? 0 : 1);
            platform.poll();
            sleep_ms(1000);
            sess = gfn.poll_session(auth, sess.session_id);
            if (sess.status == 4)
                throw std::runtime_error("GFN session failed/closed before streaming");
        }

        if (sess.signaling_url.empty())
            throw std::runtime_error("CloudMatch never supplied a signaling URL");
        ON_LOGI("session", "starting WebRTC signaling_url_length=%u media_host_length=%u media_port=%d ice_servers=%u",
                (unsigned)sess.signaling_url.size(), (unsigned)sess.media_ip.size(),
                sess.media_port, (unsigned)sess.ice_servers.size());

        opennow::WebRtcSession stream(sess, cfg, platform);
        if (!stream.start()) throw std::runtime_error("WebRTC start: " + stream.state());
        ON_LOGI("stream", "WebRTC start returned success state=%s", stream.state().c_str());
        platform.clear_text();
        ui_printf(platform, "WebRTC started. Waiting for video...\nBACK+START exits the stream.\n");

        int log_tick = 0;
        for (; stream.running();) {
            platform.poll();
            stream.poll();
            opennow::GamepadState pad;
            if (platform.read_gamepad(pad)) {
                if ((pad.buttons & 0x0030) == 0x0030) {
                    ON_LOGI("stream", "BACK+START exit requested");
                    break;
                }
                stream.send_gamepad(pad);
            }
            if (++log_tick >= 3000) {
                log_tick = 0;
                ON_LOGI("stream", "periodic state=%s rtt_ms=%d",
                        stream.state().c_str(), stream.rtt_ms());
            }
            sleep_ms(1);
        }

        ON_LOGI("stream", "stopping media and CloudMatch session");
        stream.stop();
        gfn.stop_session(auth, sess.session_id);
        ON_LOGI("session", "session shutdown complete");
        platform.clear_text();
        ui_printf(platform, "Session ended.\n");
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
