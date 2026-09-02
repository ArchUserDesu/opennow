#include "opennow/websocket.hpp"
#include "opennow/ca_bundle.hpp"
#include "opennow/logger.hpp"
#include <cstring>
#include <cctype>

#if defined(OPENNOW_XDK)
#include "opennow/tls_stream.hpp"
#include <xtl.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <mbedtls/version.h>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>
extern "C" void XeCryptRandom(BYTE* output, DWORD bytes);
#else
#include <curl/curl.h>
#endif

namespace opennow {
namespace {
std::string safe_ws_url(std::string u) { std::string::size_type q=u.find('?'); if(q!=std::string::npos)u.resize(q); return u; }
#if defined(OPENNOW_XDK)
struct XdkWsState {
    TlsStream tls;
    std::vector<unsigned char> in;
    std::vector<unsigned char> fragmented;
    unsigned char fragmented_opcode;
    XdkWsState() : fragmented_opcode(0) {}
};

std::string trim_ws(const std::string& s) {
    std::size_t a=0,b=s.size(); while(a<b&&(s[a]==' '||s[a]=='\t'||s[a]=='\r'||s[a]=='\n'))++a;
    while(b>a&&(s[b-1]==' '||s[b-1]=='\t'||s[b-1]=='\r'||s[b-1]=='\n'))--b; return s.substr(a,b-a);
}
std::string lower_ws(std::string s){for(std::size_t i=0;i<s.size();++i)s[i]=(char)std::tolower((unsigned char)s[i]);return s;}

bool make_ws_key(std::string& key) {
    unsigned char raw[16]; XeCryptRandom(raw, sizeof(raw));
    unsigned char enc[64]; size_t olen=0;
    if(mbedtls_base64_encode(enc,sizeof(enc),&olen,raw,sizeof(raw))!=0)return false;
    key.assign(reinterpret_cast<char*>(enc),olen); return true;
}
bool expected_accept(const std::string& key,std::string& out){
    std::string source=key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; unsigned char hash[20];
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
    if(mbedtls_sha1(reinterpret_cast<const unsigned char*>(source.c_str()),source.size(),hash)!=0)return false;
#else
    if(mbedtls_sha1_ret(reinterpret_cast<const unsigned char*>(source.c_str()),source.size(),hash)!=0)return false;
#endif
    unsigned char enc[64]; size_t olen=0; if(mbedtls_base64_encode(enc,sizeof(enc),&olen,hash,sizeof(hash))!=0)return false;
    out.assign(reinterpret_cast<char*>(enc),olen); return true;
}

bool ws_send_frame(XdkWsState* st,unsigned char opcode,const void* payload,std::size_t bytes,std::string& error){
    if(!st)return false; std::vector<unsigned char> frame; frame.reserve(bytes+16);
    frame.push_back((unsigned char)(0x80|opcode));
    if(bytes<126)frame.push_back((unsigned char)(0x80|bytes));
    else if(bytes<=0xffff){frame.push_back(0x80|126);frame.push_back((unsigned char)(bytes>>8));frame.push_back((unsigned char)bytes);}
    else {frame.push_back(0x80|127); for(int i=7;i>=0;--i)frame.push_back((unsigned char)(((unsigned long long)bytes)>>(i*8)));}
    unsigned char mask[4]; XeCryptRandom(mask,4); frame.insert(frame.end(),mask,mask+4);
    const unsigned char* p=static_cast<const unsigned char*>(payload); for(std::size_t i=0;i<bytes;++i)frame.push_back((unsigned char)(p[i]^mask[i&3]));
    if(!st->tls.write_all(&frame[0],frame.size())){error=st->tls.error();return false;}return true;
}

int parse_frame(XdkWsState* st,std::vector<std::string>& messages,std::string& error){
    if(st->in.size()<2)return 0; const unsigned char* p=&st->in[0]; bool fin=(p[0]&0x80)!=0; unsigned char opcode=p[0]&0x0f;
    bool masked=(p[1]&0x80)!=0; unsigned long long len=p[1]&0x7f; std::size_t off=2;
    if(len==126){if(st->in.size()<4)return 0;len=((unsigned)p[2]<<8)|p[3];off=4;}
    else if(len==127){if(st->in.size()<10)return 0;len=0;for(int i=0;i<8;++i)len=(len<<8)|p[2+i];off=10;if(len>4*1024*1024ULL){error="WebSocket frame too large";return -1;}}
    unsigned char mask[4]={0,0,0,0}; if(masked){if(st->in.size()<off+4)return 0;std::memcpy(mask,p+off,4);off+=4;}
    if(st->in.size()<off+(std::size_t)len)return 0;
    std::vector<unsigned char> payload((std::size_t)len); for(std::size_t i=0;i<(std::size_t)len;++i)payload[i]=(unsigned char)(p[off+i]^(masked?mask[i&3]:0));
    st->in.erase(st->in.begin(),st->in.begin()+off+(std::size_t)len);
    if(opcode==0x8){error="WebSocket peer closed";return -1;}
    if(opcode==0x9){if(!ws_send_frame(st,0xA,payload.empty()?NULL:&payload[0],payload.size(),error))return -1;return 1;}
    if(opcode==0xA)return 1;
    if(opcode==0x1||opcode==0x2){if(fin){if(opcode==0x1)messages.push_back(std::string(payload.begin(),payload.end()));return 1;}st->fragmented.swap(payload);st->fragmented_opcode=opcode;return 1;}
    if(opcode==0x0){st->fragmented.insert(st->fragmented.end(),payload.begin(),payload.end());if(fin){if(st->fragmented_opcode==0x1)messages.push_back(std::string(st->fragmented.begin(),st->fragmented.end()));st->fragmented.clear();st->fragmented_opcode=0;}return 1;}
    return 1;
}
#endif
} // namespace

WebSocket::WebSocket()
#if defined(OPENNOW_XDK)
    : xdk_state_(NULL), connected_(false)
#else
    : curl_(NULL), headers_(NULL), connected_(false)
#endif
{}
WebSocket::~WebSocket(){close();}

bool WebSocket::connect(const std::string& url,const std::vector<std::string>& headers){
    close(); const std::string logged=safe_ws_url(url); ON_LOGI("websocket","connect begin url=%s headers=%u",logged.c_str(),(unsigned)headers.size());
#if defined(OPENNOW_XDK)
    ParsedUrl u=parse_url(url); if(!u.valid||u.scheme!="wss"){error_="XEX WebSocket requires wss://";return false;}
    XdkWsState* st=new XdkWsState(); xdk_state_=st; if(!st->tls.connect(u.host,u.port,ca_bundle_path())){error_=st->tls.error();close();return false;}
    std::string key,accept; if(!make_ws_key(key)||!expected_accept(key,accept)){error_="WebSocket key generation failed";close();return false;}
    std::ostringstream req; req<<"GET "<<u.path<<" HTTP/1.1\r\nHost: "<<u.host; if(u.port!=443)req<<':'<<u.port;
    req<<"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "<<key<<"\r\nSec-WebSocket-Version: 13\r\n";
    for(std::size_t i=0;i<headers.size();++i){std::string low=lower_ws(headers[i]);if(low.find("host:")==0||low.find("upgrade:")==0||low.find("connection:")==0||low.find("sec-websocket-key:")==0||low.find("sec-websocket-version:")==0)continue;req<<headers[i]<<"\r\n";} req<<"\r\n";
    std::string wire=req.str(); if(!st->tls.write_all(wire.c_str(),wire.size())){error_=st->tls.error();close();return false;}
    std::string response; char b[2048]; DWORD started=GetTickCount(); while(response.find("\r\n\r\n")==std::string::npos&&GetTickCount()-started<12000){int n=st->tls.read_some(b,sizeof(b));if(n>0)response.append(b,n);else if(n==0){error_="WebSocket handshake closed";close();return false;}else if(n==-1)Sleep(1);else{error_=st->tls.error();close();return false;}if(response.size()>32768){error_="WebSocket handshake too large";close();return false;}}
    std::size_t end=response.find("\r\n\r\n"); if(end==std::string::npos){error_="WebSocket handshake timeout";close();return false;}
    std::string head=response.substr(0,end); if(head.find(" 101 ")==std::string::npos){error_="WebSocket server did not switch protocols";close();return false;}
    bool accept_ok=false; std::size_t pos=head.find("\r\n")+2; while(pos<head.size()){std::size_t e=head.find("\r\n",pos);if(e==std::string::npos)e=head.size();std::string line=head.substr(pos,e-pos);std::size_t c=line.find(':');if(c!=std::string::npos&&lower_ws(trim_ws(line.substr(0,c)))=="sec-websocket-accept")accept_ok=(trim_ws(line.substr(c+1))==accept);pos=e+2;}
    if(!accept_ok){error_="WebSocket Sec-WebSocket-Accept mismatch";close();return false;}
    if(end+4<response.size())st->in.insert(st->in.end(),response.begin()+end+4,response.end()); st->tls.set_nonblocking(true); connected_=true; ON_LOGI("websocket","connect complete url=%s",logged.c_str());return true;
#else
    CURL* c=curl_easy_init();if(!c){error_="curl init";return false;}curl_slist* hs=NULL;for(auto&h:headers)hs=curl_slist_append(hs,h.c_str());curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_HTTPHEADER,hs);curl_easy_setopt(c,CURLOPT_CONNECT_ONLY,2L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,12000L);curl_easy_setopt(c,CURLOPT_TCP_NODELAY,1L);const char* ca=ca_bundle_path();curl_easy_setopt(c,CURLOPT_CAINFO,ca);CURLcode rc=curl_easy_perform(c);if(rc!=CURLE_OK){error_=curl_easy_strerror(rc);curl_slist_free_all(hs);curl_easy_cleanup(c);return false;}curl_=c;headers_=hs;connected_=true;return true;
#endif
}

bool WebSocket::send_text(const std::string& text){if(!connected_)return false;
#if defined(OPENNOW_XDK)
    XdkWsState* st=static_cast<XdkWsState*>(xdk_state_);if(!ws_send_frame(st,0x1,text.c_str(),text.size(),error_)){connected_=false;return false;}return true;
#else
    size_t sent=0;CURLcode rc=curl_ws_send((CURL*)curl_,text.c_str(),text.size(),&sent,0,CURLWS_TEXT);if(rc!=CURLE_OK){error_=curl_easy_strerror(rc);return false;}return sent==text.size();
#endif
}

bool WebSocket::poll(std::vector<std::string>& messages){if(!connected_)return false;
#if defined(OPENNOW_XDK)
    XdkWsState* st=static_cast<XdkWsState*>(xdk_state_); char b[8192]; for(int rounds=0;rounds<16;++rounds){int n=st->tls.read_some(b,sizeof(b));if(n>0)st->in.insert(st->in.end(),b,b+n);else if(n==0){error_="WebSocket closed";connected_=false;return false;}else if(n==-2){error_=st->tls.error();connected_=false;return false;}for(;;){int parsed=parse_frame(st,messages,error_);if(parsed<0){connected_=false;return false;}if(parsed==0)break;}if(n==-1)return true;}return true;
#else
    char buf[8192];for(int n=0;n<16;n++){size_t got=0;const curl_ws_frame*meta=NULL;CURLcode rc=curl_ws_recv((CURL*)curl_,buf,sizeof(buf),&got,&meta);if(rc==CURLE_AGAIN)return true;if(rc!=CURLE_OK){error_=curl_easy_strerror(rc);connected_=false;return false;}if(got)rx_.append(buf,got);if(meta&&meta->bytesleft==0){if(meta->flags&CURLWS_TEXT)messages.push_back(rx_);rx_.clear();}if(got==0)return true;}return true;
#endif
}

void WebSocket::close(){bool was=connected_;
#if defined(OPENNOW_XDK)
    if(xdk_state_){XdkWsState*st=static_cast<XdkWsState*>(xdk_state_);if(connected_){std::string ignored;ws_send_frame(st,0x8,NULL,0,ignored);}delete st;xdk_state_=NULL;}
#else
    if(headers_){curl_slist_free_all((curl_slist*)headers_);headers_=NULL;}if(curl_){curl_easy_cleanup((CURL*)curl_);curl_=NULL;}
#endif
    connected_=false;rx_.clear();if(was)ON_LOGI("websocket","connection closed");
}
} // namespace opennow
