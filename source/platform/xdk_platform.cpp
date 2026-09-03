#include "opennow/xenon_platform.hpp"
#include "opennow/yuv_convert.hpp"
#include "opennow/logger.hpp"

#if defined(OPENNOW_XDK)

#define D3DCOMPILE_DEFINE_POSITIONT_AND_XYZRHW
#include <xtl.h>
#include <winsockx.h>
#include <xauth.h>
#include <d3d9.h>
#include <d3d9fftypes.h>
#include <d3dx9.h>
#include <xaudio2.h>

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "xnet.lib")
#pragma comment(lib, "xauth.lib")
#pragma comment(lib, "xapilib.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "xaudio2.lib")

namespace opennow {
namespace {

LPDIRECT3D9 g_d3d = NULL;
LPDIRECT3DDEVICE9 g_device = NULL;
LPDIRECT3DTEXTURE9 g_frame_texture = NULL;
LPDIRECT3DVERTEXSHADER9 g_video_vs = NULL;
LPDIRECT3DPIXELSHADER9 g_video_ps = NULL;
LPDIRECT3DVERTEXDECLARATION9 g_video_decl = NULL;
UINT g_texture_width = 0;
UINT g_texture_height = 0;
UINT g_display_width = 1280;
UINT g_display_height = 720;
std::vector<std::uint32_t> g_argb;
std::vector<std::uint32_t> g_ui_argb;
std::string g_text;
std::string g_stream_overlay;
bool g_have_video_frame = false;
float g_last_video_x = 0.0f, g_last_video_y = 0.0f, g_last_video_w = 0.0f, g_last_video_h = 0.0f;

IXAudio2* g_xaudio = NULL;
IXAudio2MasteringVoice* g_master_voice = NULL;
IXAudio2SourceVoice* g_source_voice = NULL;
bool g_voice_started = false;
unsigned g_audio_submissions = 0, g_audio_underruns = 0, g_audio_queue_drops = 0;

struct AudioSlot {
    std::vector<BYTE> bytes;
    volatile LONG in_use;
    AudioSlot() : in_use(0) {}
};
AudioSlot g_audio_slots[32];
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
void draw_stream_overlay_in_scene();

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
    if (!g_device || !g_frame_texture || !g_video_vs || !g_video_ps || !g_video_decl) return false;
    const float l=x/(float)g_display_width*2.0f-1.0f, r=(x+w)/(float)g_display_width*2.0f-1.0f;
    const float t=1.0f-y/(float)g_display_height*2.0f, b=1.0f-(y+h)/(float)g_display_height*2.0f;
    ScreenVertex v[4] = {
        {l,t,0.0f,1.0f,0xffffffffu,0.0f,0.0f}, {r,t,0.0f,1.0f,0xffffffffu,1.0f,0.0f},
        {l,b,0.0f,1.0f,0xffffffffu,0.0f,1.0f}, {r,b,0.0f,1.0f,0xffffffffu,1.0f,1.0f}
    };
    HRESULT clear_hr=g_device->Clear(0,NULL,D3DCLEAR_TARGET,D3DCOLOR_XRGB(8,8,24),1.0f,0);
    HRESULT begin_hr=g_device->BeginScene();
    if (FAILED(clear_hr)||FAILED(begin_hr)) { ON_LOGE("xdk-video","frame begin failed clear=0x%08x begin=0x%08x",(unsigned)clear_hr,(unsigned)begin_hr); return false; }
    g_device->SetTexture(0,g_frame_texture);
    g_device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    g_device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    g_device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    g_device->SetVertexDeclaration(g_video_decl); g_device->SetVertexShader(g_video_vs); g_device->SetPixelShader(g_video_ps);
    HRESULT hr = g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(ScreenVertex));
    g_device->SetTexture(0,NULL);
    if(SUCCEEDED(hr)&&!g_stream_overlay.empty())draw_stream_overlay_in_scene();
    HRESULT end_hr=g_device->EndScene();
    if (FAILED(hr)) return false;
    HRESULT present_hr=g_device->Present(NULL,NULL,NULL,NULL);
    if(FAILED(end_hr)||FAILED(present_hr)){ON_LOGE("xdk-video","frame submit failed draw=0x%08x end=0x%08x present=0x%08x",(unsigned)hr,(unsigned)end_hr,(unsigned)present_hr);return false;}
    return true;
}

bool init_video_shaders() {
    static const char* vs="struct I{float4 p:POSITION0;float4 c:COLOR0;float2 uv:TEXCOORD0;};struct O{float4 p:POSITION0;float4 c:COLOR0;float2 uv:TEXCOORD0;};O main(I i){O o;o.p=i.p;o.c=i.c;o.uv=i.uv;return o;}";
    static const char* ps="sampler2D s0:register(s0);float4 main(float2 uv:TEXCOORD0,float4 c:COLOR0):COLOR0{return tex2D(s0,uv)*c;}";
    LPD3DXBUFFER code=NULL, errors=NULL; HRESULT hr=D3DXCompileShader(vs,(UINT)strlen(vs),NULL,NULL,"main","vs_2_0",0,&code,&errors,NULL);
    if(FAILED(hr)||!code){ON_LOGE("xdk-video","vertex shader compile failed hr=0x%08x detail=%s",(unsigned)hr,errors?(const char*)errors->GetBufferPointer():"none");if(errors)errors->Release();return false;}
    hr=g_device->CreateVertexShader((DWORD*)code->GetBufferPointer(),&g_video_vs);code->Release();if(errors){errors->Release();errors=NULL;}if(FAILED(hr)){ON_LOGE("xdk-video","CreateVertexShader failed hr=0x%08x",(unsigned)hr);return false;}
    hr=D3DXCompileShader(ps,(UINT)strlen(ps),NULL,NULL,"main","ps_2_0",0,&code,&errors,NULL);
    if(FAILED(hr)||!code){ON_LOGE("xdk-video","pixel shader compile failed hr=0x%08x detail=%s",(unsigned)hr,errors?(const char*)errors->GetBufferPointer():"none");if(errors)errors->Release();return false;}
    hr=g_device->CreatePixelShader((DWORD*)code->GetBufferPointer(),&g_video_ps);code->Release();if(errors)errors->Release();if(FAILED(hr)){ON_LOGE("xdk-video","CreatePixelShader failed hr=0x%08x",(unsigned)hr);return false;}
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,20,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    hr=g_device->CreateVertexDeclaration(elements,&g_video_decl);if(FAILED(hr)){ON_LOGE("xdk-video","CreateVertexDeclaration failed hr=0x%08x",(unsigned)hr);return false;}
    ON_LOGI("xdk-video","stream video shaders initialized");return true;
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

void draw_stream_overlay_in_scene(){
    if(!g_device||g_stream_overlay.empty())return;
    static unsigned overlay_frames=0,overlay_failures=0;++overlay_frames;
    const int scale=g_display_width>=1000?3:2,cw=6*scale,ch=9*scale,left=42,top=54;
    const int panel_right=std::min<int>((int)g_display_width-30,760),panel_bottom=std::min<int>((int)g_display_height-30,top+ch*18);
    D3DRECT panel={24,28,panel_right,panel_bottom};
    HRESULT panel_hr=g_device->Clear(1,&panel,D3DCLEAR_TARGET,D3DCOLOR_XRGB(10,14,22),1.0f,0);
    std::vector<D3DRECT> pixels,selected_pixels,highlights;pixels.reserve(g_stream_overlay.size()*20);selected_pixels.reserve(256);int x=left,y=top;bool line_start=true,line_selected=false;
    for(std::size_t i=0;i<g_stream_overlay.size();++i){char c=g_stream_overlay[i];if(c=='\r')continue;if(c=='\n'){x=left;y+=ch;line_start=true;line_selected=false;continue;}if(x+cw>panel_right-20){x=left;y+=ch;line_start=true;line_selected=false;}if(y+7*scale>=panel_bottom-15)break;if(line_start){line_selected=(c=='>');if(line_selected){D3DRECT h={left-8,y-4,panel_right-18,y+7*scale+4};highlights.push_back(h);}line_start=false;}const unsigned char*g=glyph5x7(c);for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(1u<<(4-col))){D3DRECT r={x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale};if(line_selected)selected_pixels.push_back(r);else pixels.push_back(r);}x+=cw;}
    HRESULT highlight_hr=highlights.empty()?S_OK:g_device->Clear((DWORD)highlights.size(),&highlights[0],D3DCLEAR_TARGET,D3DCOLOR_XRGB(24,86,70),1.0f,0);
    HRESULT text_hr=pixels.empty()?S_OK:g_device->Clear((DWORD)pixels.size(),&pixels[0],D3DCLEAR_TARGET,D3DCOLOR_XRGB(242,242,242),1.0f,0);
    HRESULT selected_hr=selected_pixels.empty()?S_OK:g_device->Clear((DWORD)selected_pixels.size(),&selected_pixels[0],D3DCLEAR_TARGET,D3DCOLOR_XRGB(120,255,180),1.0f,0);
    if(overlay_frames==1)ON_LOGI("ui","first stream overlay frame drawn chars=%u glyph_rects=%u panel=0x%08x text=0x%08x",(unsigned)g_stream_overlay.size(),(unsigned)pixels.size(),(unsigned)panel_hr,(unsigned)text_hr);
    if((FAILED(panel_hr)||FAILED(highlight_hr)||FAILED(text_hr)||FAILED(selected_hr))&&overlay_failures++<10)ON_LOGE("ui","stream overlay draw failed frame=%u panel=0x%08x highlight=0x%08x text=0x%08x selected=0x%08x",overlay_frames,(unsigned)panel_hr,(unsigned)highlight_hr,(unsigned)text_hr,(unsigned)selected_hr);
}

void pixel(int x,int y,std::uint32_t color){ if(x>=0&&y>=0&&x<(int)g_display_width&&y<(int)g_display_height) g_ui_argb[(std::size_t)y*g_display_width+x]=color; }
void draw_char(int x,int y,char c,int scale,std::uint32_t color){ const unsigned char* g=glyph5x7(c); for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(1u<<(4-col)))for(int yy=0;yy<scale;++yy)for(int xx=0;xx<scale;++xx)pixel(x+col*scale+xx,y+row*scale+yy,color); }

void render_text() {
    static unsigned renders=0,failures=0;
    if(!g_device){if(failures++<3)ON_LOGE("ui","render skipped: D3D device is null");return;}
    const int scale = g_display_width >= 1000 ? 3 : 2;
    const int cw=6*scale, ch=9*scale, left=36, top=30;
    int x=left,y=top; const int right=(int)g_display_width-36;
    std::vector<D3DRECT> pixels;
    pixels.reserve(g_text.size()*20);
    for(std::size_t i=0;i<g_text.size();++i){
        char c=g_text[i]; if(c=='\r')continue; if(c=='\n'){x=left;y+=ch;continue;}
        if(x+cw>right){x=left;y+=ch;} if(y+7*scale>=(int)g_display_height-20)break;
        const unsigned char* glyph=glyph5x7(c);
        for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(glyph[row]&(1u<<(4-col))){D3DRECT r={x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale};pixels.push_back(r);}
        x+=cw;
    }
    HRESULT bg=g_device->Clear(0,NULL,D3DCLEAR_TARGET,D3DCOLOR_XRGB(8,8,24),1.0f,0);
    HRESULT fg=pixels.empty()?S_OK:g_device->Clear((DWORD)pixels.size(),&pixels[0],D3DCLEAR_TARGET,D3DCOLOR_XRGB(242,242,242),1.0f,0);
    HRESULT shown=g_device->Present(NULL,NULL,NULL,NULL);++renders;
    bool drawn=SUCCEEDED(bg)&&SUCCEEDED(fg)&&SUCCEEDED(shown);
    if(!drawn){if(failures++<10)ON_LOGE("ui","direct render failed sequence=%u bg=0x%08x fg=0x%08x present=0x%08x chars=%u rects=%u",renders,(unsigned)bg,(unsigned)fg,(unsigned)shown,(unsigned)g_text.size(),(unsigned)pixels.size());}
    else {static bool logged_nonempty=false;if(renders==1)ON_LOGI("ui","first frame presented display=%ux%u chars=%u",g_display_width,g_display_height,(unsigned)g_text.size());if(!logged_nonempty&&!g_text.empty()){logged_nonempty=true;ON_LOGI("ui","first non-empty text frame presented chars=%u",(unsigned)g_text.size());}}
}

bool init_video() {
    ON_LOGI("xdk-video","initialization begin sdk=%u",(unsigned)D3D_SDK_VERSION);
    g_d3d=Direct3DCreate9(D3D_SDK_VERSION); if(!g_d3d){ON_LOGE("xdk-video","Direct3DCreate9 returned null");return false;}
    XVIDEO_MODE mode; ZeroMemory(&mode,sizeof(mode)); XGetVideoMode(&mode);
    g_display_width=std::min<DWORD>(mode.dwDisplayWidth?mode.dwDisplayWidth:1280,1280);
    g_display_height=std::min<DWORD>(mode.dwDisplayHeight?mode.dwDisplayHeight:720,720);
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp,sizeof(pp));
    pp.BackBufferWidth=g_display_width; pp.BackBufferHeight=g_display_height;
    /* Match the XDK's known-good title presentation layout.  The previous
       zero FrontBufferFormat is accepted by CreateDevice but can present a
       permanently black scanout on retail kernels/custom dashboards. */
    pp.BackBufferFormat=(D3DFORMAT)MAKESRGBFMT(D3DFMT_A8R8G8B8);
    pp.FrontBufferFormat=(D3DFORMAT)MAKESRGBFMT(D3DFMT_LE_X8R8G8B8);
    pp.BackBufferCount=1; pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality=0; pp.EnableAutoDepthStencil=FALSE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    ON_LOGI("xdk-video","video mode reported=%ux%u widescreen=%d progressive=%d selected=%ux%u",mode.dwDisplayWidth,mode.dwDisplayHeight,mode.fIsWideScreen?1:0,mode.fIsHiDef?1:0,g_display_width,g_display_height);
    HRESULT hr=g_d3d->CreateDevice(0,D3DDEVTYPE_HAL,NULL,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&g_device); if(FAILED(hr)||!g_device){ON_LOGE("xdk-video","CreateDevice failed hr=0x%08x device=%p",(unsigned)hr,g_device);return false;}ON_LOGI("xdk-video","CreateDevice complete device=%p",g_device);return init_video_shaders();
}

bool init_audio() {
    ON_LOGI("xdk-audio","initialization begin rate=48000 channels=2");
    HRESULT hr=XAudio2Create(&g_xaudio,0,XAUDIO2_DEFAULT_PROCESSOR); if(FAILED(hr)||!g_xaudio){ON_LOGE("xdk-audio","XAudio2Create failed hr=0x%08x engine=%p",(unsigned)hr,g_xaudio);return false;}
    hr=g_xaudio->CreateMasteringVoice(&g_master_voice,2,48000); if(FAILED(hr)){ON_LOGE("xdk-audio","CreateMasteringVoice failed hr=0x%08x",(unsigned)hr);return false;}
    WAVEFORMATEX fmt; ZeroMemory(&fmt,sizeof(fmt)); fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=2;fmt.nSamplesPerSec=48000;fmt.wBitsPerSample=16;fmt.nBlockAlign=4;fmt.nAvgBytesPerSec=192000;
    hr=g_xaudio->CreateSourceVoice(&g_source_voice,&fmt,0,XAUDIO2_DEFAULT_FREQ_RATIO,&g_voice_callback,NULL,NULL);if(FAILED(hr)||!g_source_voice){ON_LOGE("xdk-audio","CreateSourceVoice failed hr=0x%08x voice=%p",(unsigned)hr,g_source_voice);return false;}g_voice_started=false;ON_LOGI("xdk-audio","initialization complete engine=%p voice=%p prebuffer_packets=3 max_queue=24",g_xaudio,g_source_voice);return true;
}

bool init_network() {
    XNetStartupParams p; ZeroMemory(&p,sizeof(p)); p.cfgSizeOfStruct=sizeof(p); p.cfgFlags=XNET_STARTUP_BYPASS_SECURITY;
    INT rc=XNetStartup(&p); if(rc){ON_LOGE("xdk-net","XNetStartup failed rc=%d",rc);return false;}
    WSADATA wsa; ZeroMemory(&wsa,sizeof(wsa)); rc=WSAStartup(MAKEWORD(2,2),&wsa); if(rc){ON_LOGE("xdk-net","WSAStartup failed rc=%d",rc);XNetCleanup();return false;}
    BOOL insecure_allowed=XAuthInsecureSocketsAllowed();
    ON_LOGI("xdk-net","insecure_sockets_allowed=%d xnet_flags=0x%02x",insecure_allowed?1:0,(unsigned)p.cfgFlags);
    ON_LOGI("xdk-net","public Internet access uses XNet bypass, XEX privilege, and per-socket 0x5801 marking");
    XNADDR addr; ZeroMemory(&addr,sizeof(addr)); DWORD st=XNET_GET_XNADDR_PENDING,start=GetTickCount(); while(st==XNET_GET_XNADDR_PENDING&&GetTickCount()-start<15000){st=XNetGetTitleXnAddr(&addr);Sleep(50);} if(st==XNET_GET_XNADDR_NONE||st==XNET_GET_XNADDR_PENDING){ON_LOGE("xdk-net","network address unavailable status=0x%08x",(unsigned)st);return false;} ON_LOGI("xdk-net","title network ready status=0x%08x ip=0x%08x",(unsigned)st,(unsigned)addr.ina.s_addr);return true;
}
float normalize_thumb(SHORT v){return v>=0?std::min(1.0f,(float)v/32767.0f):std::max(-1.0f,(float)v/32768.0f);}
}

bool XenonPlatform::init(){log_init();ON_LOGI("boot","OpenNOW Xbox-kernel/XEX platform starting");if(!init_video()){ON_LOGE("boot","Direct3D init failed");return false;}if(!init_audio()){ON_LOGE("boot","XAudio2 init failed");return false;}if(!init_network()){ON_LOGE("boot","XNet/Winsock init failed");return false;}ON_LOGI("boot","XEX platform ready display=%ux%u",g_display_width,g_display_height);return true;}
void XenonPlatform::poll(){}
bool XenonPlatform::read_gamepad(GamepadState& s){XINPUT_STATE xs;ZeroMemory(&xs,sizeof(xs));if(XInputGetState(0,&xs)!=ERROR_SUCCESS)return false;const XINPUT_GAMEPAD&g=xs.Gamepad;s.controller_id=0;s.controller_bitmap=1;s.left_trigger=g.bLeftTrigger;s.right_trigger=g.bRightTrigger;s.lx=normalize_thumb(g.sThumbLX);s.ly=-normalize_thumb(g.sThumbLY);s.rx=normalize_thumb(g.sThumbRX);s.ry=-normalize_thumb(g.sThumbRY);enum{UP=1,DOWN=2,LEFT=4,RIGHT=8,START=0x10,BACK=0x20,LS=0x40,RS=0x80,LB=0x100,RB=0x200,A=0x1000,B=0x2000,X=0x4000,Y=0x8000};WORD b=g.wButtons;s.buttons=((b&XINPUT_GAMEPAD_DPAD_UP)?UP:0)|((b&XINPUT_GAMEPAD_DPAD_DOWN)?DOWN:0)|((b&XINPUT_GAMEPAD_DPAD_LEFT)?LEFT:0)|((b&XINPUT_GAMEPAD_DPAD_RIGHT)?RIGHT:0)|((b&XINPUT_GAMEPAD_START)?START:0)|((b&XINPUT_GAMEPAD_BACK)?BACK:0)|((b&XINPUT_GAMEPAD_LEFT_THUMB)?LS:0)|((b&XINPUT_GAMEPAD_RIGHT_THUMB)?RS:0)|((b&XINPUT_GAMEPAD_LEFT_SHOULDER)?LB:0)|((b&XINPUT_GAMEPAD_RIGHT_SHOULDER)?RB:0)|((b&XINPUT_GAMEPAD_A)?A:0)|((b&XINPUT_GAMEPAD_B)?B:0)|((b&XINPUT_GAMEPAD_X)?X:0)|((b&XINPUT_GAMEPAD_Y)?Y:0);static WORD last_special=0;WORD special=(WORD)(b&(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_Y));if(special!=last_special){ON_LOGI("controller","special buttons raw=0x%04x mapped=0x%04x back=%d start=%d y=%d packet=%u",(unsigned)b,(unsigned)s.buttons,(b&XINPUT_GAMEPAD_BACK)?1:0,(b&XINPUT_GAMEPAD_START)?1:0,(b&XINPUT_GAMEPAD_Y)?1:0,(unsigned)xs.dwPacketNumber);last_special=special;}return true;}

bool XenonPlatform::present(const VideoFrame& f){if(!g_device||f.width<=0||f.height<=0)return false;if(!to_argb8888(f,g_argb)||!upload_argb(g_argb,(UINT)f.width,(UINT)f.height))return false;float sx=(float)g_display_width/f.width,sy=(float)g_display_height/f.height,sc=std::min(sx,sy);g_last_video_w=f.width*sc;g_last_video_h=f.height*sc;g_last_video_x=(g_display_width-g_last_video_w)*.5f;g_last_video_y=(g_display_height-g_last_video_h)*.5f;g_have_video_frame=true;return draw_current_texture(g_last_video_x,g_last_video_y,g_last_video_w,g_last_video_h);}

bool XenonPlatform::play_pcm48_stereo(const std::int16_t* pcm,std::size_t frames){if(!g_source_voice||!pcm||!frames)return false;XAUDIO2_VOICE_STATE before;ZeroMemory(&before,sizeof(before));g_source_voice->GetState(&before);if(g_voice_started&&before.BuffersQueued==0){g_source_voice->Stop(0);g_voice_started=false;++g_audio_underruns;if(g_audio_underruns<=5||g_audio_underruns%50==0)ON_LOGW("xdk-audio","playback underrun count=%u; rebuffering",g_audio_underruns);}if(before.BuffersQueued>=24){++g_audio_queue_drops;if(g_audio_queue_drops<=5||g_audio_queue_drops%50==0)ON_LOGW("xdk-audio","latency cap dropped packet count=%u queued=%u",g_audio_queue_drops,(unsigned)before.BuffersQueued);return false;}AudioSlot*slot=NULL;unsigned count=sizeof(g_audio_slots)/sizeof(g_audio_slots[0]);for(unsigned i=0;i<count;++i){AudioSlot&c=g_audio_slots[(g_audio_cursor+i)%count];if(InterlockedCompareExchange(&c.in_use,1,0)==0){slot=&c;g_audio_cursor=(g_audio_cursor+i+1)%count;break;}}if(!slot)return false;std::size_t bytes=frames*4;slot->bytes.resize(bytes);for(std::size_t i=0;i<frames*2;++i){std::uint16_t v=(std::uint16_t)pcm[i];slot->bytes[i*2]=(BYTE)(v&255);slot->bytes[i*2+1]=(BYTE)(v>>8);}XAUDIO2_BUFFER b;ZeroMemory(&b,sizeof(b));b.AudioBytes=(UINT32)slot->bytes.size();b.pAudioData=&slot->bytes[0];b.pContext=slot;HRESULT hr=g_source_voice->SubmitSourceBuffer(&b);if(FAILED(hr)){InterlockedExchange(&slot->in_use,0);return false;}++g_audio_submissions;XAUDIO2_VOICE_STATE after;ZeroMemory(&after,sizeof(after));g_source_voice->GetState(&after);if(!g_voice_started&&after.BuffersQueued>=3){hr=g_source_voice->Start(0);if(FAILED(hr)){ON_LOGE("xdk-audio","source voice restart failed hr=0x%08x",(unsigned)hr);return false;}g_voice_started=true;ON_LOGI("xdk-audio","playback started/restarted queued=%u submissions=%u underruns=%u",(unsigned)after.BuffersQueued,g_audio_submissions,g_audio_underruns);}if(g_audio_submissions==1||g_audio_submissions%1000==0)ON_LOGI("xdk-audio","PCM submit count=%u frames=%u queued=%u first_sample=%d",g_audio_submissions,(unsigned)frames,(unsigned)after.BuffersQueued,(int)pcm[0]);return true;}
void XenonPlatform::log(const char*s){ON_LOGI("platform","%s",s?s:"");}
void XenonPlatform::clear_text(){g_have_video_frame=false;g_text.clear();render_text();}
void XenonPlatform::write_text(const char*s){if(!s)return;g_text+=s;if(g_text.size()>16384)g_text.erase(0,g_text.size()-12288);render_text();}
void XenonPlatform::set_stream_overlay(const char*s){const bool was_visible=!g_stream_overlay.empty();g_stream_overlay=s?s:"";const bool visible=!g_stream_overlay.empty();bool repainted=false;if(g_have_video_frame)repainted=draw_current_texture(g_last_video_x,g_last_video_y,g_last_video_w,g_last_video_h);if(was_visible!=visible)ON_LOGI("ui","stream overlay %s immediate_repaint=%d chars=%u",visible?"shown":"hidden",repainted?1:0,(unsigned)g_stream_overlay.size());}
}
#endif
