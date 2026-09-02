#include "opennow/xenon_platform.hpp"
#include "opennow/yuv_convert.hpp"
#include "opennow/logger.hpp"

#if defined(OPENNOW_XDK)

#include <xtl.h>
#include <winsockx.h>
#include <d3d9.h>
#include <xaudio2.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "xnet.lib")
#pragma comment(lib, "xapilib.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "xaudio2.lib")

namespace opennow {
namespace {

LPDIRECT3D9 g_d3d = NULL;
LPDIRECT3DDEVICE9 g_device = NULL;
LPDIRECT3DTEXTURE9 g_frame_texture = NULL;
UINT g_texture_width = 0;
UINT g_texture_height = 0;
UINT g_display_width = 1280;
UINT g_display_height = 720;
std::vector<std::uint32_t> g_argb;
std::vector<std::uint32_t> g_ui_argb;
std::string g_text;

IXAudio2* g_xaudio = NULL;
IXAudio2MasteringVoice* g_master_voice = NULL;
IXAudio2SourceVoice* g_source_voice = NULL;

struct AudioSlot {
    std::vector<BYTE> bytes;
    volatile LONG in_use;
    AudioSlot() : in_use(0) {}
};
AudioSlot g_audio_slots[12];
unsigned int g_audio_cursor = 0;

class VoiceCallback : public IXAudio2VoiceCallback {
public:
    STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32) {}
    STDMETHOD_(void, OnVoiceProcessingPassEnd)() {}
    STDMETHOD_(void, OnStreamEnd)() {}
    STDMETHOD_(void, OnBufferStart)(void*) {}
    STDMETHOD_(void, OnBufferEnd)(void* context) { AudioSlot* s = static_cast<AudioSlot*>(context); if (s) InterlockedExchange(&s->in_use, 0); }
    STDMETHOD_(void, OnLoopEnd)(void*) {}
    STDMETHOD_(void, OnVoiceError)(void* context, HRESULT) { AudioSlot* s = static_cast<AudioSlot*>(context); if (s) InterlockedExchange(&s->in_use, 0); }
};
VoiceCallback g_voice_callback;

struct ScreenVertex { float x,y,z,rhw; DWORD color; float u,v; };
#define OPENNOW_SCREEN_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

void release_video_texture() {
    if (g_frame_texture) { g_frame_texture->Release(); g_frame_texture = NULL; }
    g_texture_width = g_texture_height = 0;
}

bool ensure_video_texture(UINT width, UINT height) {
    if (g_frame_texture && g_texture_width == width && g_texture_height == height) return true;
    release_video_texture();
    if (!g_device) return false;
    HRESULT hr = g_device->CreateTexture(width, height, 1, 0, D3DFMT_LIN_A8R8G8B8,
                                         D3DPOOL_DEFAULT, &g_frame_texture, NULL);
    if (FAILED(hr)) { ON_LOGE("xdk-video", "CreateTexture failed hr=0x%08x size=%ux%u", (unsigned)hr, width, height); return false; }
    g_texture_width = width; g_texture_height = height; return true;
}

bool upload_argb(const std::vector<std::uint32_t>& src, UINT width, UINT height) {
    if (!ensure_video_texture(width,height) || src.size() < (std::size_t)width * height) return false;
    D3DLOCKED_RECT r; ZeroMemory(&r,sizeof(r));
    if (FAILED(g_frame_texture->LockRect(0,&r,NULL,0))) return false;
    for (UINT y=0; y<height; ++y)
        std::memcpy(static_cast<BYTE*>(r.pBits)+y*r.Pitch, &src[(std::size_t)y*width], (std::size_t)width*4);
    g_frame_texture->UnlockRect(0); return true;
}

bool draw_current_texture(float x, float y, float w, float h) {
    if (!g_device || !g_frame_texture) return false;
    ScreenVertex v[4] = {
        {x-0.5f,   y-0.5f,   0.0f,1.0f,0xffffffffu,0.0f,0.0f},
        {x+w-0.5f, y-0.5f,   0.0f,1.0f,0xffffffffu,1.0f,0.0f},
        {x-0.5f,   y+h-0.5f, 0.0f,1.0f,0xffffffffu,0.0f,1.0f},
        {x+w-0.5f, y+h-0.5f, 0.0f,1.0f,0xffffffffu,1.0f,1.0f}
    };
    g_device->Clear(0,NULL,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,0,0),1.0f,0);
    if (FAILED(g_device->BeginScene())) return false;
    g_device->SetTexture(0,g_frame_texture);
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    g_device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    g_device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    g_device->SetFVF(OPENNOW_SCREEN_FVF);
    HRESULT hr = g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(ScreenVertex));
    g_device->SetTexture(0,NULL);
    g_device->EndScene();
    if (FAILED(hr)) return false;
    g_device->Present(NULL,NULL,NULL,NULL); return true;
}

/* 5x7 font. Each byte is a 5-bit row. Lowercase is displayed as uppercase. */
const unsigned char* glyph5x7(char c) {
    static const unsigned char blank[7]={0,0,0,0,0,0,0};
    static const unsigned char unknown[7]={14,17,1,2,4,0,4};
    static const unsigned char A[7]={14,17,17,31,17,17,17},B[7]={30,17,17,30,17,17,30},C[7]={14,17,16,16,16,17,14};
    static const unsigned char D[7]={30,17,17,17,17,17,30},E[7]={31,16,16,30,16,16,31},F[7]={31,16,16,30,16,16,16};
    static const unsigned char G[7]={14,17,16,23,17,17,15},H[7]={17,17,17,31,17,17,17},I[7]={14,4,4,4,4,4,14};
    static const unsigned char J[7]={7,2,2,2,2,18,12},K[7]={17,18,20,24,20,18,17},L[7]={16,16,16,16,16,16,31};
    static const unsigned char M[7]={17,27,21,21,17,17,17},N[7]={17,25,21,19,17,17,17},O[7]={14,17,17,17,17,17,14};
    static const unsigned char P[7]={30,17,17,30,16,16,16},Q[7]={14,17,17,17,21,18,13},R[7]={30,17,17,30,20,18,17};
    static const unsigned char S[7]={15,16,16,14,1,1,30},T[7]={31,4,4,4,4,4,4},U[7]={17,17,17,17,17,17,14};
    static const unsigned char V[7]={17,17,17,17,17,10,4},W[7]={17,17,17,21,21,21,10},X[7]={17,17,10,4,10,17,17};
    static const unsigned char Y[7]={17,17,10,4,4,4,4},Z[7]={31,1,2,4,8,16,31};
    static const unsigned char N0[7]={14,17,19,21,25,17,14},N1[7]={4,12,4,4,4,4,14},N2[7]={14,17,1,2,4,8,31};
    static const unsigned char N3[7]={30,1,1,14,1,1,30},N4[7]={2,6,10,18,31,2,2},N5[7]={31,16,16,30,1,1,30};
    static const unsigned char N6[7]={14,16,16,30,17,17,14},N7[7]={31,1,2,4,8,8,8},N8[7]={14,17,17,14,17,17,14},N9[7]={14,17,17,15,1,1,14};
    static const unsigned char dot[7]={0,0,0,0,0,12,12},colon[7]={0,12,12,0,12,12,0},dash[7]={0,0,0,31,0,0,0};
    static const unsigned char slash[7]={1,2,2,4,8,8,16},backslash[7]={16,8,8,4,2,2,1},under[7]={0,0,0,0,0,0,31};
    static const unsigned char lbr[7]={14,8,8,8,8,8,14},rbr[7]={14,2,2,2,2,2,14},lp[7]={2,4,8,8,8,4,2},rp[7]={8,4,2,2,2,4,8};
    static const unsigned char eq[7]={0,31,0,31,0,0,0},plus[7]={0,4,4,31,4,4,0},comma[7]={0,0,0,0,0,4,8};
    static const unsigned char apos[7]={4,4,0,0,0,0,0},quote[7]={10,10,0,0,0,0,0},hash[7]={10,31,10,10,31,10,0};
    static const unsigned char at[7]={14,17,23,21,23,16,14},pct[7]={17,2,4,8,16,17,0},amp[7]={12,18,20,8,21,18,13};
    static const unsigned char star[7]={0,21,14,31,14,21,0},excl[7]={4,4,4,4,4,0,4},lt[7]={2,4,8,16,8,4,2},gt[7]={8,4,2,1,2,4,8};
    if(c>='a'&&c<='z') c=(char)(c-'a'+'A');
    if(c>='A'&&c<='Z'){ static const unsigned char* letters[26]={A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z}; return letters[c-'A']; }
    if(c>='0'&&c<='9'){ static const unsigned char* nums[10]={N0,N1,N2,N3,N4,N5,N6,N7,N8,N9}; return nums[c-'0']; }
    switch(c){case ' ':return blank;case '.':return dot;case ':':return colon;case '-':return dash;case '/':return slash;case '\\':return backslash;case '_':return under;case '[':return lbr;case ']':return rbr;case '(':return lp;case ')':return rp;case '=':return eq;case '+':return plus;case ',':return comma;case '\'':return apos;case '"':return quote;case '#':return hash;case '@':return at;case '%':return pct;case '&':return amp;case '*':return star;case '!':return excl;case '<':return lt;case '>':return gt;default:return unknown;}
}

void pixel(int x,int y,std::uint32_t color){ if(x>=0&&y>=0&&x<(int)g_display_width&&y<(int)g_display_height) g_ui_argb[(std::size_t)y*g_display_width+x]=color; }
void draw_char(int x,int y,char c,int scale,std::uint32_t color){ const unsigned char* g=glyph5x7(c); for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(1u<<(4-col)))for(int yy=0;yy<scale;++yy)for(int xx=0;xx<scale;++xx)pixel(x+col*scale+xx,y+row*scale+yy,color); }

void render_text() {
    if(!g_device) return;
    g_ui_argb.assign((std::size_t)g_display_width*g_display_height,0xff080808u);
    const int scale = g_display_width >= 1000 ? 3 : 2;
    const int cw=6*scale, ch=9*scale, left=36, top=30;
    int x=left,y=top; const int right=(int)g_display_width-36;
    for(std::size_t i=0;i<g_text.size();++i){ char c=g_text[i]; if(c=='\r')continue; if(c=='\n'){x=left;y+=ch;continue;} if(x+cw>right){x=left;y+=ch;} if(y+7*scale>=(int)g_display_height-20)break; draw_char(x,y,c,scale,0xfff2f2f2u); x+=cw; }
    if(upload_argb(g_ui_argb,g_display_width,g_display_height)) draw_current_texture(0,0,(float)g_display_width,(float)g_display_height);
}

bool init_video() {
    g_d3d=Direct3DCreate9(D3D_SDK_VERSION); if(!g_d3d)return false;
    XVIDEO_MODE mode; ZeroMemory(&mode,sizeof(mode)); XGetVideoMode(&mode);
    g_display_width=std::min<DWORD>(mode.dwDisplayWidth?mode.dwDisplayWidth:1280,1280);
    g_display_height=std::min<DWORD>(mode.dwDisplayHeight?mode.dwDisplayHeight:720,720);
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp,sizeof(pp)); pp.BackBufferWidth=g_display_width; pp.BackBufferHeight=g_display_height; pp.BackBufferFormat=D3DFMT_X8R8G8B8; pp.BackBufferCount=1; pp.EnableAutoDepthStencil=FALSE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    HRESULT hr=g_d3d->CreateDevice(0,D3DDEVTYPE_HAL,NULL,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&g_device); return SUCCEEDED(hr)&&g_device;
}

bool init_audio() {
    HRESULT hr=XAudio2Create(&g_xaudio,0,XAUDIO2_DEFAULT_PROCESSOR); if(FAILED(hr)||!g_xaudio)return false;
    hr=g_xaudio->CreateMasteringVoice(&g_master_voice,2,48000); if(FAILED(hr))return false;
    WAVEFORMATEX fmt; ZeroMemory(&fmt,sizeof(fmt)); fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=2;fmt.nSamplesPerSec=48000;fmt.wBitsPerSample=16;fmt.nBlockAlign=4;fmt.nAvgBytesPerSec=192000;
    hr=g_xaudio->CreateSourceVoice(&g_source_voice,&fmt,0,XAUDIO2_DEFAULT_FREQ_RATIO,&g_voice_callback,NULL,NULL); return SUCCEEDED(hr)&&g_source_voice&&SUCCEEDED(g_source_voice->Start(0));
}

bool init_network() {
    XNetStartupParams p; ZeroMemory(&p,sizeof(p)); p.cfgSizeOfStruct=sizeof(p); p.cfgFlags=XNET_STARTUP_BYPASS_SECURITY;
    INT rc=XNetStartup(&p); if(rc){ON_LOGE("xdk-net","XNetStartup failed rc=%d",rc);return false;}
    WSADATA wsa; ZeroMemory(&wsa,sizeof(wsa)); rc=WSAStartup(MAKEWORD(2,2),&wsa); if(rc){ON_LOGE("xdk-net","WSAStartup failed rc=%d",rc);XNetCleanup();return false;}
    XNADDR addr; ZeroMemory(&addr,sizeof(addr)); DWORD st=XNET_GET_XNADDR_PENDING,start=GetTickCount(); while(st==XNET_GET_XNADDR_PENDING&&GetTickCount()-start<15000){st=XNetGetTitleXnAddr(&addr);Sleep(50);} if(st==XNET_GET_XNADDR_NONE||st==XNET_GET_XNADDR_PENDING){ON_LOGE("xdk-net","network address unavailable status=0x%08x",(unsigned)st);return false;} ON_LOGI("xdk-net","title network ready status=0x%08x ip=0x%08x",(unsigned)st,(unsigned)addr.ina.s_addr);return true;
}
float normalize_thumb(SHORT v){return v>=0?std::min(1.0f,(float)v/32767.0f):std::max(-1.0f,(float)v/32768.0f);}
}

bool XenonPlatform::init(){log_init();ON_LOGI("boot","OpenNOW Xbox-kernel/XEX platform starting");if(!init_video()){ON_LOGE("boot","Direct3D init failed");return false;}if(!init_audio()){ON_LOGE("boot","XAudio2 init failed");return false;}if(!init_network()){ON_LOGE("boot","XNet/Winsock init failed");return false;}ON_LOGI("boot","XEX platform ready display=%ux%u",g_display_width,g_display_height);return true;}
void XenonPlatform::poll(){}
bool XenonPlatform::read_gamepad(GamepadState& s){XINPUT_STATE xs;ZeroMemory(&xs,sizeof(xs));if(XInputGetState(0,&xs)!=ERROR_SUCCESS)return false;const XINPUT_GAMEPAD&g=xs.Gamepad;s.controller_id=0;s.controller_bitmap=1;s.left_trigger=g.bLeftTrigger;s.right_trigger=g.bRightTrigger;s.lx=normalize_thumb(g.sThumbLX);s.ly=-normalize_thumb(g.sThumbLY);s.rx=normalize_thumb(g.sThumbRX);s.ry=-normalize_thumb(g.sThumbRY);enum{UP=1,DOWN=2,LEFT=4,RIGHT=8,START=0x10,BACK=0x20,LS=0x40,RS=0x80,LB=0x100,RB=0x200,A=0x1000,B=0x2000,X=0x4000,Y=0x8000};WORD b=g.wButtons;s.buttons=((b&XINPUT_GAMEPAD_DPAD_UP)?UP:0)|((b&XINPUT_GAMEPAD_DPAD_DOWN)?DOWN:0)|((b&XINPUT_GAMEPAD_DPAD_LEFT)?LEFT:0)|((b&XINPUT_GAMEPAD_DPAD_RIGHT)?RIGHT:0)|((b&XINPUT_GAMEPAD_START)?START:0)|((b&XINPUT_GAMEPAD_BACK)?BACK:0)|((b&XINPUT_GAMEPAD_LEFT_THUMB)?LS:0)|((b&XINPUT_GAMEPAD_RIGHT_THUMB)?RS:0)|((b&XINPUT_GAMEPAD_LEFT_SHOULDER)?LB:0)|((b&XINPUT_GAMEPAD_RIGHT_SHOULDER)?RB:0)|((b&XINPUT_GAMEPAD_A)?A:0)|((b&XINPUT_GAMEPAD_B)?B:0)|((b&XINPUT_GAMEPAD_X)?X:0)|((b&XINPUT_GAMEPAD_Y)?Y:0);return true;}

bool XenonPlatform::present(const VideoFrame& f){if(!g_device||f.width<=0||f.height<=0)return false;if(!to_argb8888(f,g_argb)||!upload_argb(g_argb,(UINT)f.width,(UINT)f.height))return false;float sx=(float)g_display_width/f.width,sy=(float)g_display_height/f.height,sc=std::min(sx,sy),w=f.width*sc,h=f.height*sc;return draw_current_texture((g_display_width-w)*.5f,(g_display_height-h)*.5f,w,h);}

bool XenonPlatform::play_pcm48_stereo(const std::int16_t* pcm,std::size_t frames){if(!g_source_voice||!pcm||!frames)return false;AudioSlot*slot=NULL;unsigned count=sizeof(g_audio_slots)/sizeof(g_audio_slots[0]);for(unsigned i=0;i<count;++i){AudioSlot&c=g_audio_slots[(g_audio_cursor+i)%count];if(InterlockedCompareExchange(&c.in_use,1,0)==0){slot=&c;g_audio_cursor=(g_audio_cursor+i+1)%count;break;}}if(!slot)return false;std::size_t bytes=frames*4;slot->bytes.resize(bytes);for(std::size_t i=0;i<frames*2;++i){std::uint16_t v=(std::uint16_t)pcm[i];slot->bytes[i*2]=(BYTE)(v&255);slot->bytes[i*2+1]=(BYTE)(v>>8);}XAUDIO2_BUFFER b;ZeroMemory(&b,sizeof(b));b.AudioBytes=(UINT32)slot->bytes.size();b.pAudioData=&slot->bytes[0];b.pContext=slot;HRESULT hr=g_source_voice->SubmitSourceBuffer(&b);if(FAILED(hr)){InterlockedExchange(&slot->in_use,0);return false;}return true;}
void XenonPlatform::log(const char*s){ON_LOGI("platform","%s",s?s:"");}
void XenonPlatform::clear_text(){g_text.clear();render_text();}
void XenonPlatform::write_text(const char*s){if(!s)return;g_text+=s;if(g_text.size()>16384)g_text.erase(0,g_text.size()-12288);render_text();}
}
#endif
