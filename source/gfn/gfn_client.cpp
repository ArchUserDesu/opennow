#include "opennow/gfn_client.hpp"
#include "opennow/json_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <stdexcept>

#if !defined(__LIBXENON__) && !defined(OPENNOW_XDK)
#include <chrono>
#include <thread>
#endif

#ifdef __LIBXENON__
extern "C" {
#include <time/time.h>
}
#elif defined(OPENNOW_XDK)
#include <xtl.h>
#endif

namespace opennow {
namespace {

static const char* const UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36 "
    "NVIDIACEFClient/HEAD/debb5919f6 GFN-PC/2.0.80.173";
static const char* const DEVICE_UA =
    "Mozilla/5.0 (X11; Linux x86_64; Steam Deck) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";
static const char* const DEVICE_CLIENT = "q61ddeJrVt7O90Nl-P-N7I36yctih4Ml6FyXLrb6j-U";
static const char* const LCARS = "ec7e38d4-03af-4b58-b131-cfb0495903ab";

static std::string number_string(long value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

static std::int64_t now_ms() {
#ifdef __LIBXENON__
    return (std::int64_t)time(NULL) * 1000;
#elif defined(OPENNOW_XDK)
    return (std::int64_t)time(NULL) * 1000 + (std::int64_t)(GetTickCount() % 1000);
#else
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
#endif
}

static void sleep_100ms() {
#ifdef __LIBXENON__
    mdelay(100);
#elif defined(OPENNOW_XDK)
    Sleep(100);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
}

static std::string random_device_id() {
    unsigned char raw[16];
#ifdef OPENNOW_XDK
    XeCryptRandom(raw, sizeof(raw));
#else
    unsigned long seed = (unsigned long)time(NULL);
#if !defined(__LIBXENON__)
    seed ^= (unsigned long)std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
    std::srand(seed);
    for (size_t i = 0; i < sizeof(raw); ++i)
        raw[i] = (unsigned char)(std::rand() & 0xff);
#endif
    /* RFC 4122-ish v4 UUID. This is only a client device identifier. */
    raw[6] = (unsigned char)((raw[6] & 0x0f) | 0x40);
    raw[8] = (unsigned char)((raw[8] & 0x3f) | 0x80);
    char b[64];
#ifdef OPENNOW_XDK
    _snprintf(b, sizeof(b),
#else
    std::snprintf(b, sizeof(b),
#endif
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
        raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14], raw[15]);
    b[sizeof(b) - 1] = '\0';
    return std::string(b);
}

static std::vector<std::string> device_headers(const std::string& dev, bool identity) {
    std::vector<std::string> h;
    h.push_back("Origin: https://play.geforcenow.com");
    h.push_back("Referer: https://play.geforcenow.com/");
    h.push_back("Accept: application/json, text/plain, */*");
    h.push_back("Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    h.push_back(std::string("User-Agent: ") + DEVICE_UA);
    if (identity) {
        h.push_back("x-device-id: " + dev);
        h.push_back(std::string("nv-client-id: ") + DEVICE_CLIENT);
        h.push_back("nv-client-streamer: WEBRTC");
        h.push_back("nv-client-type: BROWSER");
        h.push_back("nv-client-platform-name: browser");
        h.push_back("nv-browser-type: CHROME");
        h.push_back("nv-device-os: STEAMOS");
        h.push_back("nv-device-type: CONSOLE");
        h.push_back("nv-device-model: STEAMDECK");
        h.push_back("nv-device-make: VALVE");
    }
    return h;
}

static std::vector<std::string> device_headers() {
    return device_headers(std::string(), false);
}

static std::vector<std::string> native_headers(const AuthSession& s, bool json) {
    std::vector<std::string> h;
    h.push_back("Accept: application/json");
    h.push_back(std::string("Authorization: GFNJWT ") + GfnClient::session_jwt(s));
    h.push_back(std::string("nv-client-id: ") + LCARS);
    h.push_back("nv-client-type: NATIVE");
    h.push_back("nv-client-version: 2.0.80.173");
    h.push_back("nv-client-streamer: NVIDIA-CLASSIC");
    h.push_back("nv-browser-type: CHROME");
    h.push_back("nv-device-os: WINDOWS");
    h.push_back("nv-device-type: DESKTOP");
    h.push_back("nv-device-make: UNKNOWN");
    h.push_back("nv-device-model: UNKNOWN");
    h.push_back(std::string("User-Agent: ") + UA);
    h.push_back("Origin: https://play.geforcenow.com");
    h.push_back("Referer: https://play.geforcenow.com/");
    if (json)
        h.push_back("Content-Type: application/json");
    if (!s.device_id.empty())
        h.push_back("x-device-id: " + s.device_id);
    return h;
}

static std::string lower(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = (char)std::tolower((unsigned char)s[i]);
    return s;
}

static bool is_ice_url(const std::string& u) {
    return u.find("stun:") == 0 || u.find("turn:") == 0 ||
           u.find("stuns:") == 0 || u.find("turns:") == 0;
}

static void collect_ice(json_t* v, SessionInfo& out) {
    if (!v)
        return;
    if (json_is_string(v)) {
        const char* p = json_string_value(v);
        const std::string u = p ? p : "";
        if (is_ice_url(u))
            out.ice_servers.push_back(IceServerInfo(u, "", ""));
        return;
    }
    if (json_is_array(v)) {
        size_t i;
        json_t* e;
        json_array_foreach(v, i, e) { collect_ice(e, out); }
        return;
    }
    if (!json_is_object(v))
        return;

    std::string user = js(v, "username");
    std::string cred = js(v, "credential");
    if (cred.empty())
        cred = js(v, "password");

    static const char* const direct_keys[] = {
        "urls", "url", "uri", "server", "serverUrl", "stunUrl", "turnUrl"};
    for (size_t k = 0; k < sizeof(direct_keys) / sizeof(direct_keys[0]); ++k) {
        json_t* e = json_object_get(v, direct_keys[k]);
        if (json_is_string(e)) {
            const char* p = json_string_value(e);
            const std::string u = p ? p : "";
            if (is_ice_url(u))
                out.ice_servers.push_back(IceServerInfo(u, user, cred));
        } else {
            collect_ice(e, out);
        }
    }

    static const char* const nested_keys[] = {
        "iceServers", "ice_servers", "iceServer", "rtcIceServers", "stunServers",
        "turnServers", "iceConfiguration", "rtcConfiguration", "webrtcConfiguration"};
    for (size_t k = 0; k < sizeof(nested_keys) / sizeof(nested_keys[0]); ++k)
        collect_ice(json_object_get(v, nested_keys[k]), out);
}

static std::string host_from_url(std::string u) {
    std::string::size_type p = u.find("://");
    if (p != std::string::npos)
        u.erase(0, p + 3);
    std::string::size_type slash = u.find('/');
    if (slash != std::string::npos)
        u.resize(slash);
    std::string::size_type colon = u.rfind(':');
    if (colon != std::string::npos && u.find(']') == std::string::npos)
        u.resize(colon);
    return u;
}

static int port_from_url(const std::string& u) {
    std::string::size_type p = u.find("://");
    p = p == std::string::npos ? 0 : p + 3;
    std::string::size_type c = u.find(':', p);
    std::string::size_type e = u.find('/', p);
    if (c == std::string::npos || (e != std::string::npos && c > e))
        return 0;
    return std::atoi(u.c_str() + c + 1);
}

static void parse_network(json_t* s, SessionInfo& out) {
    if (!s || !json_is_object(s))
        return;
    collect_ice(s, out);
    if (!js(s, "sessionToken").empty())
        out.session_token = js(s, "sessionToken");
    if (!js(s, "serverIp").empty())
        out.server_ip = js(s, "serverIp");
    if (!js(s, "signalingUrl").empty())
        out.signaling_url = js(s, "signalingUrl");

    json_t* ci = json_object_get(s, "connectionInfo");
    if (json_is_array(ci)) {
        const int wanted_values[] = {2, 17, 14};
        for (size_t w = 0; w < sizeof(wanted_values) / sizeof(wanted_values[0]); ++w) {
            const int wanted = wanted_values[w];
            size_t i;
            json_t* x;
            json_array_foreach(ci, i, x) {
                collect_ice(x, out);
                if (ji(x, "usage", -1) != wanted)
                    continue;
                std::string rp = js(x, "resourcePath");
                std::string ip = js(x, "ip");
                if (ip.empty())
                    ip = host_from_url(rp);
                int port = ji(x, "port", 0);
                if (!port)
                    port = port_from_url(rp);
                if (out.media_ip.empty() && !ip.empty() && port > 0) {
                    out.media_ip = ip;
                    out.media_port = port;
                }
                if (out.signaling_url.empty() && wanted == 14) {
                    if (rp.find("wss://") == 0)
                        out.signaling_url = rp;
                    else if (rp.find("https://") == 0)
                        out.signaling_url = "wss://" + rp.substr(8);
                    else if (!ip.empty())
                        out.signaling_url = "wss://" + ip + ":443/nvst/";
                }
            }
        }
    }
    if (out.signaling_url.empty() && !out.server_ip.empty())
        out.signaling_url = "wss://" + out.server_ip + ":443/nvst/";
}

static int status_value(json_t* v) {
    if (json_is_integer(v))
        return (int)json_integer_value(v);
    if (!json_is_string(v))
        return -1;
    std::string s = lower(json_string_value(v));
    if (s == "queued")
        return 0;
    if (s.find("provision") != std::string::npos || s.find("setup") != std::string::npos ||
        s.find("launch") != std::string::npos)
        return 1;
    if (s == "active" || s == "ready" || s == "paused")
        return 2;
    if (s == "streaming" || s == "playing" || s == "connected")
        return 3;
    if (s.find("fail") != std::string::npos || s.find("error") != std::string::npos ||
        s.find("closed") != std::string::npos)
        return 4;
    return -1;
}

static SessionInfo parse_session_response(const std::string& b) {
    JsonPtr r = parse_json(b);
    json_t* s = json_object_get(r.get(), "session");
    if (!json_is_object(s))
        s = json_object_get(r.get(), "sessionData");
    if (!json_is_object(s))
        s = r.get();
    SessionInfo o;
    o.session_id = js(s, "sessionId");
    if (o.session_id.empty())
        o.session_id = js(r.get(), "sessionId");
    o.status = status_value(json_object_get(s, "status"));
    o.queue_position = ji(s, "queuePosition", 0);
    o.app_patching = jb(s, "appPatching", false);
    parse_network(r.get(), o);
    if (s != r.get())
        parse_network(s, o);
    return o;
}

static void append_metadata(json_t* meta, const char* key, const char* value) {
    json_t* m = json_object();
    json_object_set_new(m, "key", json_string(key));
    json_object_set_new(m, "value", json_string(value));
    json_array_append_new(meta, m);
}

static std::string build_session_body(const GameInfo& g, const AuthSession& s, const StreamConfig& c) {
    json_t* root = json_object();
    json_t* req = json_object();
    long long app = 0;
#ifdef OPENNOW_XDK
    app = _strtoi64(g.launch_app_id.c_str(), NULL, 10);
#else
    app = std::strtoll(g.launch_app_id.c_str(), NULL, 10);
#endif
    json_object_set_new(req, "appId", json_integer(app));
    json_object_set_new(req, "cmsId", json_string(g.launch_app_id.c_str()));
    json_object_set_new(req, "internalTitle",
                        g.internal_title.empty() ? json_null() : json_string(g.internal_title.c_str()));
    json_object_set_new(req, "networkTestSessionId", json_null());
    json_object_set_new(req, "parentSessionId", json_null());
    json_object_set_new(req, "clientIdentification", json_string("GFN-PC"));
    json_object_set_new(req, "deviceHashId", json_string(s.device_id.c_str()));
    json_object_set_new(req, "clientVersion", json_string("30.0"));
    json_object_set_new(req, "clientPlatformName", json_string("windows"));
    json_object_set_new(req, "availableSupportedControllers", json_array());
    json_object_set_new(req, "sdkVersion", json_string("1.0"));
    json_object_set_new(req, "streamerVersion", json_integer(1));
    json_object_set_new(req, "useOps", json_true());
    json_object_set_new(req, "audioMode", json_integer(2));
    json_object_set_new(req, "sdrHdrMode", json_integer(0));
    json_object_set_new(req, "clientDisplayHdrCapabilities", json_null());
    json_object_set_new(req, "surroundAudioInfo", json_integer(0));
    json_object_set_new(req, "remoteControllersBitmap", json_integer(1));
    json_object_set_new(req, "enhancedStreamMode", json_integer(1));
    json_object_set_new(req, "appLaunchMode", json_integer(1));
    json_object_set_new(req, "secureRTSPSupported", json_false());
    json_object_set_new(req, "partnerCustomData", json_string(""));
    json_object_set_new(req, "accountLinked", json_true());
    json_object_set_new(req, "enablePersistingInGameSettings", json_boolean(c.persist_game_settings));
    json_object_set_new(req, "userAge", json_integer(26));
    json_object_set_new(req, "clientTimezoneOffset", json_integer(0));

    json_t* f = json_object();
    json_object_set_new(f, "reflex", json_false());
    json_object_set_new(f, "bitDepth", json_integer(0));
    json_object_set_new(f, "cloudGsync", json_false());
    json_object_set_new(f, "enabledL4S", json_false());
    json_object_set_new(f, "mouseMovementFlags", json_integer(0));
    json_object_set_new(f, "trueHdr", json_false());
    json_object_set_new(f, "supportedHidDevices", json_integer(0));
    json_object_set_new(f, "profile", json_integer(0));
    json_object_set_new(f, "fallbackToLogicalResolution", json_false());
    json_object_set_new(f, "hidDevices", json_null());
    json_object_set_new(f, "chromaFormat", json_integer(0));
    json_object_set_new(f, "prefilterMode", json_integer(0));
    json_object_set_new(f, "prefilterSharpness", json_integer(0));
    json_object_set_new(f, "prefilterNoiseReduction", json_integer(0));
    json_object_set_new(f, "hudStreamingMode", json_integer(0));
    json_object_set_new(f, "sdrColorSpace", json_integer(2));
    json_object_set_new(f, "hdrColorSpace", json_integer(0));
    json_object_set_new(f, "maxBitrateKbps", json_integer(c.bitrate_kbps));
    json_object_set_new(f, "codec", json_integer(1)); /* H.264 */
    json_object_set_new(f, "vsync", json_false());
    json_object_set_new(f, "dynamicStreamingMode", json_integer(3));
    json_object_set_new(f, "audioChannelCount", json_integer(2));
    json_object_set_new(req, "requestedStreamingFeatures", f);

    json_t* meta = json_array();
    append_metadata(meta, "SubSessionId", "xenon");
    append_metadata(meta, "wssignaling", "1");
    append_metadata(meta, "GSStreamerType", "WebRTC");
    json_object_set_new(req, "metaData", meta);

    json_t* mons = json_array();
    json_t* mon = json_object();
    json_object_set_new(mon, "monitorId", json_integer(0));
    json_object_set_new(mon, "positionX", json_integer(0));
    json_object_set_new(mon, "positionY", json_integer(0));
    json_object_set_new(mon, "widthInPixels", json_integer(c.width));
    json_object_set_new(mon, "heightInPixels", json_integer(c.height));
    json_object_set_new(mon, "framesPerSecond", json_integer(c.fps));
    json_object_set_new(mon, "sdrHdrMode", json_integer(0));
    json_object_set_new(mon, "displayData", json_null());
    json_object_set_new(mon, "hdr10PlusGamingData", json_null());
    json_object_set_new(mon, "dpi", json_integer(100));
    json_array_append_new(mons, mon);
    json_object_set_new(req, "clientRequestMonitorSettings", mons);

    json_object_set_new(root, "sessionRequestData", req);
    std::string out = dump_json(root);
    json_decref(root);
    return out;
}

struct ProviderPriorityLess {
    bool operator()(const LoginProvider& a, const LoginProvider& b) const {
        return a.priority < b.priority;
    }
};

} // namespace

std::string GfnClient::session_jwt(const AuthSession& s) {
    return !s.tokens.id_token.empty() ? s.tokens.id_token : s.tokens.access_token;
}

std::vector<LoginProvider> GfnClient::fetch_login_providers() const {
    std::vector<std::string> headers;
    headers.push_back("Accept: application/json");
    HttpResponse r = http_.get("https://pcs.geforcenow.com/v1/serviceUrls", headers);
    if (r.status_code != 200)
        throw std::runtime_error("serviceUrls HTTP " + number_string(r.status_code));

    JsonPtr j = parse_json(r.body);
    json_t* info = json_object_get(j.get(), "gfnServiceInfo");
    json_t* a = info ? json_object_get(info, "gfnServiceEndpoints") : NULL;
    std::vector<LoginProvider> out;
    if (json_is_array(a)) {
        size_t i;
        json_t* e;
        json_array_foreach(a, i, e) {
            LoginProvider p;
            p.idp_id = js(e, "idpId");
            p.code = js(e, "loginProviderCode");
            p.display_name = js(e, "loginProviderDisplayName");
            p.streaming_service_url = js(e, "streamingServiceUrl");
            p.priority = ji(e, "loginProviderPriority", 999);
            if (!p.idp_id.empty() && !p.streaming_service_url.empty())
                out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end(), ProviderPriorityLess());
    return out;
}

AuthSession GfnClient::login_qr(
    const LoginProvider& p,
    const std::function<void(const QrLoginChallenge&)>& cb,
    const std::function<bool()>& cancel) const {
    AuthSession s;
    s.provider = p;
    s.device_id = random_device_id();

    std::string body = "client_id=" + HttpClient::form_escape(DEVICE_CLIENT) +
                       "&scope=" + HttpClient::form_escape("openid consent email tk_client age") +
                       "&device_id=" + HttpClient::form_escape(s.device_id) +
                       "&display_name=OpenNOW-Xbox360&idp_id=" + HttpClient::form_escape(p.idp_id);
    HttpResponse ar = http_.post("https://login.nvidia.com/device/authorize",
                                 device_headers(s.device_id, true), body);
    if (ar.status_code != 200)
        throw std::runtime_error("device authorize HTTP " + number_string(ar.status_code) + " " + ar.body);

    JsonPtr aj = parse_json(ar.body);
    std::string dc = js(aj.get(), "device_code");
    QrLoginChallenge q;
    q.user_code = js(aj.get(), "user_code");
    q.verification_uri = js(aj.get(), "verification_uri");
    q.verification_uri_complete = js(aj.get(), "verification_uri_complete");
    q.interval_seconds = std::max(1, ji(aj.get(), "interval", 5));
    q.expires_at_ms = now_ms() + (std::int64_t)ji(aj.get(), "expires_in", 300) * 1000;
    if (cb)
        cb(q);

    while (now_ms() < q.expires_at_ms) {
        for (int n = 0; n < q.interval_seconds * 10; ++n) {
            if (cancel && cancel())
                throw std::runtime_error("login cancelled");
            sleep_100ms();
        }
        std::string tb = "grant_type=" + HttpClient::form_escape("urn:ietf:params:oauth:grant-type:device_code") +
                         "&device_code=" + HttpClient::form_escape(dc) +
                         "&client_id=" + HttpClient::form_escape(DEVICE_CLIENT);
        HttpResponse tr = http_.post("https://login.nvidia.com/token", device_headers(), tb);
        JsonPtr tj = parse_json(tr.body);
        if (tr.status_code == 200) {
            s.tokens.access_token = js(tj.get(), "access_token");
            s.tokens.refresh_token = js(tj.get(), "refresh_token");
            s.tokens.id_token = js(tj.get(), "id_token");
            s.tokens.client_token = js(tj.get(), "client_token");
            s.tokens.auth_client_id = DEVICE_CLIENT;
            s.tokens.expires_at_ms = now_ms() + (std::int64_t)ji(tj.get(), "expires_in", 86400) * 1000;

            std::vector<std::string> uh;
            uh.push_back("Accept: application/json");
            uh.push_back(std::string("Authorization: Bearer ") + s.tokens.access_token);
            uh.push_back(std::string("User-Agent: ") + DEVICE_UA);
            HttpResponse ui = http_.get("https://login.nvidia.com/userinfo", uh);
            if (ui.status_code == 200) {
                JsonPtr uj = parse_json(ui.body);
                s.user.user_id = js(uj.get(), "sub");
                s.user.display_name = js(uj.get(), "preferred_username");
                s.user.email = js(uj.get(), "email");
            }
            return s;
        }
        std::string e = js(tj.get(), "error");
        if (e == "authorization_pending")
            continue;
        if (e == "slow_down") {
            q.interval_seconds += 5;
            continue;
        }
        throw std::runtime_error("QR login: " + e + " " + js(tj.get(), "error_description"));
    }
    throw std::runtime_error("QR login expired");
}

AuthSession GfnClient::refresh(AuthSession s) const {
    if (s.tokens.expires_at_ms > now_ms() + 10 * 60 * 1000)
        return s;
    if (s.tokens.refresh_token.empty())
        throw std::runtime_error("session expired and has no refresh token");

    const std::string client_id = s.tokens.auth_client_id.empty() ? DEVICE_CLIENT : s.tokens.auth_client_id;
    std::string body = "grant_type=refresh_token&refresh_token=" + HttpClient::form_escape(s.tokens.refresh_token) +
                       "&client_id=" + HttpClient::form_escape(client_id);
    HttpResponse r = http_.post("https://login.nvidia.com/token", device_headers(), body);
    if (r.status_code != 200)
        throw std::runtime_error("refresh HTTP " + number_string(r.status_code));

    JsonPtr j = parse_json(r.body);
    const std::string old_id = s.tokens.id_token;
    const std::string old_ref = s.tokens.refresh_token;
    s.tokens.access_token = js(j.get(), "access_token");
    s.tokens.refresh_token = js(j.get(), "refresh_token");
    s.tokens.id_token = js(j.get(), "id_token");
    if (s.tokens.refresh_token.empty())
        s.tokens.refresh_token = old_ref;
    if (s.tokens.id_token.empty())
        s.tokens.id_token = old_id;
    s.tokens.expires_at_ms = now_ms() + (std::int64_t)ji(j.get(), "expires_in", 86400) * 1000;
    return s;
}

std::vector<GameInfo> GfnClient::fetch_public_games(const std::string& filter) const {
    std::vector<std::string> headers;
    headers.push_back("Accept: application/json");
    HttpResponse r = http_.get(
        "https://static.nvidiagrid.net/supported-public-game-list/locales/gfnpc-en-US.json", headers);
    if (r.status_code != 200)
        throw std::runtime_error("public catalog HTTP " + number_string(r.status_code));

    JsonPtr j = parse_json(r.body);
    json_t* a = j.get();
    if (json_is_object(a)) {
        static const char* const keys[] = {"games", "items", "apps"};
        for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) {
            json_t* x = json_object_get(a, keys[k]);
            if (json_is_array(x)) {
                a = x;
                break;
            }
        }
    }

    std::vector<GameInfo> out;
    if (!json_is_array(a))
        return out;
    const std::string f = lower(filter);
    size_t i;
    json_t* e;
    json_array_foreach(a, i, e) {
        GameInfo g;
        g.id = js(e, "id");
        if (g.id.empty()) g.id = js(e, "gameId");
        g.title = js(e, "title");
        if (g.title.empty()) g.title = js(e, "name");
        g.publisher = js(e, "publisher");
        g.launch_app_id = js(e, "launchAppId");
        if (g.launch_app_id.empty()) g.launch_app_id = js(e, "cmsId");
        if (g.launch_app_id.empty()) g.launch_app_id = g.id;
        g.store = js(e, "store");
        g.internal_title = js(e, "internalTitle");
        json_t* vars = json_object_get(e, "variants");
        if (json_is_array(vars)) {
            size_t vi;
            json_t* v;
            json_array_foreach(vars, vi, v) {
                GameVariant gv;
                gv.id = js(v, "id");
                gv.store = js(v, "appStore");
                if (gv.store.empty()) gv.store = js(v, "store");
                gv.internal_title = js(v, "internalTitle");
                g.variants.push_back(gv);
                if (g.launch_app_id.empty()) g.launch_app_id = gv.id;
            }
        }
        if (g.title.empty())
            continue;
        if (!f.empty() && lower(g.title).find(f) == std::string::npos)
            continue;
        out.push_back(g);
    }
    return out;
}

std::vector<GameInfo> GfnClient::fetch_catalog_games(AuthSession& s, const std::string& search) const {
    s = refresh(s);
    std::string base = s.provider.streaming_service_url;
    if (base.empty()) base = "https://prod.cloudmatchbeta.nvidiagrid.net/";
    if (base[base.size() - 1] != '/') base += '/';

    std::string vpc = "GFN-PC";
    HttpResponse si = http_.get(base + "v2/serverInfo", native_headers(s, false));
    if (si.status_code == 200) {
        try {
            JsonPtr r = parse_json(si.body);
            std::string x = js(json_object_get(r.get(), "requestStatus"), "serverId");
            if (!x.empty()) vpc = x;
        } catch (...) {
        }
    }

    static const char* const browse =
        "query GetFilterBrowseResults($vpcId: String!, $locale: String!, $sortString: String!, "
        "$fetchCount: Int!, $cursor: String!, $filters: AppFilterFields!) { "
        "apps(vpcId: $vpcId, language: $locale, orderBy: $sortString, first: $fetchCount, "
        "after: $cursor, filters: $filters) { pageInfo { hasNextPage endCursor totalCount } "
        "items { id title longDescription shortDescription publisherName images { KEY_ART GAME_BOX_ART "
        "TV_BANNER HERO_IMAGE } variants { id appStore gfn { status library { status selected } } } "
        "gfn { playabilityState minimumMembershipTierLabel } } } }";
    static const char* const searchq =
        "query GetSearchFilterResults($vpcId: String!, $locale: String!, $sortString: String!, "
        "$fetchCount: Int!, $cursor: String!, $searchString: String!, $filters: AppFilterFields!) { "
        "apps(vpcId: $vpcId, language: $locale, orderBy: $sortString, first: $fetchCount, "
        "after: $cursor, searchQuery: $searchString, filters: $filters) { "
        "pageInfo { hasNextPage endCursor totalCount } items { id title longDescription shortDescription "
        "publisherName images { KEY_ART GAME_BOX_ART TV_BANNER HERO_IMAGE } variants { id appStore "
        "gfn { status library { status selected } } } gfn { playabilityState minimumMembershipTierLabel } } } }";

    std::vector<GameInfo> out;
    std::string cursor;
    for (int page = 0; page < 3; ++page) {
        json_t* root = json_object();
        json_t* vars = json_object();
        json_object_set_new(root, "query", json_string(search.empty() ? browse : searchq));
        json_object_set_new(vars, "vpcId", json_string(vpc.c_str()));
        json_object_set_new(vars, "locale", json_string("en_US"));
        json_object_set_new(vars, "sortString", json_string("itemMetadata.relevance:DESC,sortName:ASC"));
        json_object_set_new(vars, "fetchCount", json_integer(120));
        json_object_set_new(vars, "cursor", json_string(cursor.c_str()));
        json_object_set_new(vars, "filters", json_object());
        if (!search.empty())
            json_object_set_new(vars, "searchString", json_string(search.c_str()));
        json_object_set_new(root, "variables", vars);
        std::string body = dump_json(root);
        json_decref(root);

        std::vector<std::string> h = native_headers(s, true);
        HttpResponse rr = http_.post("https://games.geforce.com/graphql", h, body);
        if (rr.status_code != 200)
            throw std::runtime_error("GFN catalog GraphQL HTTP " + number_string(rr.status_code) + " " + rr.body);

        JsonPtr j = parse_json(rr.body);
        json_t* data = json_object_get(j.get(), "data");
        json_t* apps = data ? json_object_get(data, "apps") : NULL;
        json_t* items = apps ? json_object_get(apps, "items") : NULL;
        if (!json_is_array(items))
            throw std::runtime_error("GFN catalog response has no data.apps.items");

        size_t i;
        json_t* a;
        json_array_foreach(items, i, a) {
            GameInfo g;
            g.id = js(a, "id");
            g.title = js(a, "title");
            g.publisher = js(a, "publisherName");
            json_t* im = json_object_get(a, "images");
            static const char* const image_keys[] = {"KEY_ART", "GAME_BOX_ART", "TV_BANNER", "HERO_IMAGE"};
            for (size_t ik = 0; ik < sizeof(image_keys) / sizeof(image_keys[0]); ++ik) {
                if (g.image_url.empty())
                    g.image_url = js(im, image_keys[ik]);
            }
            json_t* va = json_object_get(a, "variants");
            if (json_is_array(va)) {
                size_t vi;
                json_t* v;
                json_array_foreach(va, vi, v) {
                    GameVariant gv;
                    gv.id = js(v, "id");
                    gv.store = js(v, "appStore");
                    json_t* vg = json_object_get(v, "gfn");
                    json_t* lib = vg ? json_object_get(vg, "library") : NULL;
                    gv.selected = jb(lib, "selected", false);
                    g.variants.push_back(gv);
                    if (g.launch_app_id.empty() || gv.selected) {
                        g.launch_app_id = gv.id;
                        g.store = gv.store;
                    }
                }
            }
            if (!g.id.empty() && !g.title.empty())
                out.push_back(g);
        }

        json_t* pi = apps ? json_object_get(apps, "pageInfo") : NULL;
        const bool more = jb(pi, "hasNextPage", false);
        const std::string next = js(pi, "endCursor");
        if (!more || next.empty() || next == cursor)
            break;
        cursor = next;
    }
    return out;
}

SessionInfo GfnClient::start_session(AuthSession& s, const GameInfo& g, const StreamConfig& c) const {
    s = refresh(s);
    std::string base = s.provider.streaming_service_url;
    if (base.empty()) base = "https://prod.cloudmatchbeta.nvidiagrid.net/";
    if (base[base.size() - 1] != '/') base += '/';
    const std::string url = base + "v2/session?keyboardLayout=en-US_qwerty&languageCode=en_US";
    HttpResponse r = http_.post(url, native_headers(s, true), build_session_body(g, s, c));
    if (r.status_code < 200 || r.status_code >= 300)
        throw std::runtime_error("start session HTTP " + number_string(r.status_code) + " " + r.body);
    return parse_session_response(r.body);
}

SessionInfo GfnClient::poll_session(AuthSession& s, const std::string& id) const {
    s = refresh(s);
    std::string base = s.provider.streaming_service_url;
    if (base.empty()) base = "https://prod.cloudmatchbeta.nvidiagrid.net/";
    if (base[base.size() - 1] != '/') base += '/';
    HttpResponse r = http_.get(base + "v2/session/" + id, native_headers(s, false));
    if (r.status_code < 200 || r.status_code >= 300)
        throw std::runtime_error("poll session HTTP " + number_string(r.status_code));
    SessionInfo x = parse_session_response(r.body);
    if (x.session_id.empty()) x.session_id = id;
    return x;
}

void GfnClient::stop_session(AuthSession& s, const std::string& id) const {
    if (id.empty()) return;
    std::string base = s.provider.streaming_service_url;
    if (base.empty()) base = "https://prod.cloudmatchbeta.nvidiagrid.net/";
    if (base[base.size() - 1] != '/') base += '/';
    http_.del(base + "v2/session/" + id, native_headers(s, false));
}

} // namespace opennow
