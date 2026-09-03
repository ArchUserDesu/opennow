#include "opennow/xenon_platform.hpp"
#include "opennow/yuv_convert.hpp"
#include "opennow/logger.hpp"
#ifdef __LIBXENON__
extern "C" {
#include <xenos/xenos.h>
#include <xenos/xe.h>
#include <console/console.h>
#include <network/network.h>
#include <input/input.h>
#include <xenon_sound/sound.h>
#include <ppc/cache.h>
#include <usb/usbmain.h>
#include <libfat/fat.h>
#include <time/time.h>
}
#include <cstdio>
#include <cstring>
#include <vector>
namespace opennow {
static XenosDevice g_xe;static bool g_video=false;static std::vector<std::uint32_t> g_argb;static std::vector<std::uint8_t> g_audio_le;
static int g_present_width=-1,g_present_height=-1;
static unsigned long long g_presented_frames=0,g_audio_packets=0;
static std::size_t tiled_offset(int x,int y,int pitch_pixels){
    return (std::size_t)(((y>>5)*32*pitch_pixels+((x>>5)<<10)+(x&3)+
           ((y&1)<<2)+(((x&31)>>2)<<3)+(((y&31)>>1)<<6))^((y&8)<<2));
}
static std::uint32_t argb_to_xenos_bgra(std::uint32_t v){
    return ((v&0x000000ffu)<<24)|((v&0x0000ff00u)<<8)|
           ((v&0x00ff0000u)>>8)|((v&0xff000000u)>>24);
}
bool XenonPlatform::init(){
    // Mount storage before touching video. If a later hardware initializer
    // freezes, the last completed stage is still present in opennow.log.
    usb_init();
    for(int i=0;i<50;i++){usb_do_poll();mdelay(10);}
    const bool storage=fatInitDefault();
    log_init();
    ON_LOGI("boot","OpenNOW-Xenon diagnostic build starting");
    ON_LOGI("boot","USB initialized; FAT mount=%s",storage?"success":"failure");
    ON_LOGI("boot","stage VIDEO_XENOS_BEGIN mode=AUTO");
    xenos_init(VIDEO_MODE_AUTO);
    ON_LOGI("boot","stage VIDEO_XENOS_OK");
    console_init();
    ON_LOGI("boot","stage TEXT_CONSOLE_OK");
    Xe_Init(&g_xe);
    g_video=true;
    XenosSurface* fb=Xe_GetFramebufferSurface(&g_xe);
    ON_LOGI("boot","stage XE_DEVICE_OK framebuffer=%p %dx%d pitch=%d bypp=%d tiled=%d",
            fb,fb?fb->width:0,fb?fb->height:0,fb?fb->wpitch:0,fb?fb->bypp:0,fb?fb->tiled:0);
    ON_LOGI("boot","stage AUDIO_BEGIN");
    xenon_sound_init();
    ON_LOGI("boot","stage AUDIO_OK free_bytes=%d",xenon_sound_get_free());
    ON_LOGI("boot","stage NETWORK_BEGIN");
    int n=network_init();
    ON_LOGI("boot","stage NETWORK_INIT_RETURN result=%d failure_constant=%d",n,NETWORK_INIT_FAILURE);
    network_print_config();
    ON_LOGI("boot","stage NETWORK_CONFIG_PRINTED");
    if(!storage)ON_LOGW("boot","no writable FAT storage was mounted; persistent files may fail");
    if(n==NETWORK_INIT_FAILURE)ON_LOGE("boot","network initialization failed");
    else ON_LOGI("boot","platform initialization complete");
    return n!=NETWORK_INIT_FAILURE;
}
void XenonPlatform::poll(){usb_do_poll();network_poll();}
bool XenonPlatform::read_gamepad(GamepadState& s){controller_data_s d{};if(get_controller_data(&d,0)!=0)return false;s.controller_id=0;s.controller_bitmap=1;s.left_trigger=d.lt;s.right_trigger=d.rt;s.lx=d.s1_x/32767.0f;s.ly=-d.s1_y/32767.0f;s.rx=d.s2_x/32767.0f;s.ry=-d.s2_y/32767.0f;enum{UP=0x0001,DOWN=0x0002,LEFT=0x0004,RIGHT=0x0008,START=0x0010,BACK=0x0020,LS=0x0040,RS=0x0080,LB=0x0100,RB=0x0200,LOGO=0x0400,A=0x1000,B=0x2000,X=0x4000,Y=0x8000};s.buttons=(d.up?UP:0)|(d.down?DOWN:0)|(d.left?LEFT:0)|(d.right?RIGHT:0)|(d.start?START:0)|(d.back?BACK:0)|(d.s1_z?LS:0)|(d.s2_z?RS:0)|(d.lb?LB:0)|(d.rb?RB:0)|(d.logo?LOGO:0)|(d.a?A:0)|(d.b?B:0)|(d.x?X:0)|(d.y?Y:0);return true;}
bool XenonPlatform::present(const VideoFrame& f){
    if(!g_video){ON_LOGE("video","present requested before video initialization");return false;}
    if(!to_argb8888(f,g_argb)){ON_LOGE("video","conversion failed format=%d size=%dx%d",(int)f.format,f.width,f.height);return false;}
    XenosSurface* fb=Xe_GetFramebufferSurface(&g_xe);
    if(!fb||!fb->base||fb->bypp!=4||!fb->tiled){ON_LOGE("video","invalid framebuffer fb=%p base=%p bypp=%d tiled=%d",fb,fb?fb->base:nullptr,fb?fb->bypp:0,fb?fb->tiled:0);return false;}
    auto* dst=(std::uint32_t*)fb->base;
    const int pitch_pixels=fb->wpitch/4;
    const int w=(f.width<fb->width?f.width:fb->width);
    const int h=(f.height<fb->height?f.height:fb->height);
    const int xo=(fb->width-w)/2,yo=(fb->height-h)/2;
    if(w!=g_present_width||h!=g_present_height){
        const int tiled_height=(fb->height+31)&~31;
        std::memset(dst,0,(std::size_t)fb->wpitch*(std::size_t)tiled_height);
        g_present_width=w;g_present_height=h;
        ON_LOGI("video","presentation configured source=%dx%d display=%dx%d pitch=%d offset=%d,%d",f.width,f.height,fb->width,fb->height,fb->wpitch,xo,yo);
    }
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        const std::uint32_t pixel=g_argb[(std::size_t)y*(std::size_t)f.width+(std::size_t)x];
        dst[tiled_offset(xo+x,yo+y,pitch_pixels)]=argb_to_xenos_bgra(pixel);
    }
    memdcbst(dst,(std::size_t)fb->wpitch*(std::size_t)((fb->height+31)&~31));
    ++g_presented_frames;
    if(g_presented_frames==1||g_presented_frames%1800==0)ON_LOGI("video","presented frames=%llu",g_presented_frames);
    return true;
}
bool XenonPlatform::play_pcm48_stereo(const std::int16_t* pcm,std::size_t frames){if(!pcm||!frames)return false;const std::size_t samples=frames*2;g_audio_le.resize(samples*2);for(std::size_t i=0;i<samples;i++){const std::uint16_t v=(std::uint16_t)pcm[i];g_audio_le[i*2]=(std::uint8_t)(v&0xff);g_audio_le[i*2+1]=(std::uint8_t)(v>>8);}if(g_audio_le.size()>(std::size_t)xenon_sound_get_free()){if(g_audio_packets%500==0)ON_LOGW("audio","output buffer full wanted=%u free=%d",(unsigned)g_audio_le.size(),xenon_sound_get_free());return false;}xenon_sound_submit(g_audio_le.data(),(int)g_audio_le.size());++g_audio_packets;if(g_audio_packets==1||g_audio_packets%3000==0)ON_LOGI("audio","submitted packets=%llu latest_frames=%u",g_audio_packets,(unsigned)frames);return true;}
void XenonPlatform::log(const char* s){ON_LOGI("platform","%s",s?s:"");}
void XenonPlatform::clear_text(){std::printf("\033[2J\033[H");}
void XenonPlatform::write_text(const char* s){if(s)std::printf("%s",s);}
void XenonPlatform::set_stream_overlay(const char* s){if(s)std::printf("%s",s);}
}
#elif !defined(OPENNOW_XDK)
#include <cstdio>
namespace opennow {bool XenonPlatform::init(){return true;}void XenonPlatform::poll(){}bool XenonPlatform::read_gamepad(GamepadState&){return false;}bool XenonPlatform::present(const VideoFrame&){return true;}bool XenonPlatform::play_pcm48_stereo(const std::int16_t*,std::size_t){return true;}void XenonPlatform::log(const char* s){std::puts(s?s:"");}void XenonPlatform::clear_text(){}void XenonPlatform::write_text(const char* s){if(s)std::puts(s);}void XenonPlatform::set_stream_overlay(const char*){}}
#endif
