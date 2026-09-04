#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace opennow {
struct LoginProvider { std::string idp_id,code,display_name,streaming_service_url; int priority; LoginProvider():priority(0){} };
struct AuthTokens { std::string access_token,refresh_token,id_token,client_token,auth_client_id; std::int64_t expires_at_ms,client_token_expires_at_ms; AuthTokens():expires_at_ms(0),client_token_expires_at_ms(0){} };
struct AuthUser { std::string user_id,display_name,email; };
struct AuthSession { LoginProvider provider; AuthTokens tokens; AuthUser user; std::string device_id; };
struct QrLoginChallenge { std::string user_code,verification_uri,verification_uri_complete; std::int64_t expires_at_ms; int interval_seconds; QrLoginChallenge():expires_at_ms(0),interval_seconds(5){} };
struct GameVariant { std::string id,store,internal_title; bool selected; GameVariant():selected(false){} };
struct GameInfo { std::string id,title,publisher,image_url,launch_app_id,store,internal_title; std::vector<GameVariant> variants; bool in_library; GameInfo():in_library(false){} };
struct IceServerInfo { std::string url,username,credential; IceServerInfo(){} IceServerInfo(const std::string&u,const std::string&n,const std::string&c):url(u),username(n),credential(c){} };
struct SessionInfo { std::string session_id; int status,queue_position; bool app_patching; std::string session_token,server_ip,signaling_url,media_ip; int media_port; std::vector<IceServerInfo> ice_servers; SessionInfo():status(-1),queue_position(0),app_patching(false),media_port(0){} };
// Match OpenNOW Vita's hardware-oriented defaults: native 960x544, 60 FPS,
// with an 8 Mbps initial link ceiling.  On Xenon this cuts pixels/frame by
// ~43% versus 720p while restoring motion cadence, which is a much better fit
// for the single-threaded software H.264 decoder than 720p60.
struct StreamConfig { int width,height,fps,bitrate_kbps; bool persist_game_settings; StreamConfig():width(960),height(544),fps(60),bitrate_kbps(8000),persist_game_settings(true){} };
}