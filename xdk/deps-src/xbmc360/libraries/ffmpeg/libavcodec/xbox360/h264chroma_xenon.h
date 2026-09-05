/* Xenon VMX128 8-bit H.264 chroma interpolation.
 * Included after the scalar templates so initialization can validate the
 * accelerated functions against this exact FFmpeg version on the console.
 * LGPL-2.1-or-later, like the containing FFmpeg implementation.
 */
#include <xtl.h>
#include <string.h>
#include "libavutil/log.h"

static __forceinline __vector4 xenon_chroma_load(const uint8_t *p)
{
    return __vperm(__lvx(p, 0), __lvx(p, 15), __lvsl(p, 0));
}

static __forceinline __vector4 xenon_chroma_lerp(__vector4 a,__vector4 b,int f)
{
    // VMX128 omits AltiVec's integer multiply. Fractions are only 0..7:
    // synthesize 8*a + f*(b-a) with shifts and adds, without losing bits.
    __vector4 delta=__vsubuhm(b,a);
    __vector4 sum=__vslh(a,__vspltish(3));
    if(f&1) sum=__vadduhm(sum,delta);
    if(f&2) sum=__vadduhm(sum,__vslh(delta,__vspltish(1)));
    if(f&4) sum=__vadduhm(sum,__vslh(delta,__vspltish(2)));
    return sum;
}

static void xenon_chroma8(uint8_t *dst, uint8_t *src, int stride,
                          int h, int x, int y, int average)
{
    __declspec(align(16)) uint8_t result[16];
    __vector4 round, shift, zero;
    int row;
    zero=__vzero();shift=__vspltish(6);
    round=__vslh(__vspltish(1),__vspltish(5));
    for(row=0;row<h;++row) {
        __vector4 top=xenon_chroma_load(src);
        __vector4 right=__vsldoi(top,top,1);
        __vector4 sum=xenon_chroma_lerp(__vmrghb(zero,top),__vmrghb(zero,right),x);
        if(y) {
            __vector4 bottom=xenon_chroma_load(src+stride);
            right=__vsldoi(bottom,bottom,1);
            bottom=xenon_chroma_lerp(__vmrghb(zero,bottom),__vmrghb(zero,right),x);
            sum=xenon_chroma_lerp(sum,bottom,y);
        } else sum=__vslh(sum,__vspltish(3));
        // Weights sum to 64, so the unsigned 16-bit accumulator never wraps.
        sum=__vsrh(__vadduhm(sum,round),shift);
        sum=__vpkuhus(sum,sum);
        if(average) sum=__vavgub(sum,xenon_chroma_load(dst));
        __stvx(sum,result,0);
        // Write exactly eight bytes; adjacent blocks can belong to another
        // decoding thread. Never read/modify/write an entire destination vector.
        memcpy(dst,result,8);
        src+=stride;dst+=stride;
    }
}

static void xenon_put_chroma8(uint8_t*d,uint8_t*s,int stride,int h,int x,int y)
{ xenon_chroma8(d,s,stride,h,x,y,0); }
static void xenon_avg_chroma8(uint8_t*d,uint8_t*s,int stride,int h,int x,int y)
{ xenon_chroma8(d,s,stride,h,x,y,1); }

static int xenon_chroma_check(void)
{
    __declspec(align(16)) uint8_t src[32*18];
    __declspec(align(16)) uint8_t expected[32*18];
    __declspec(align(16)) uint8_t actual[32*18];
    int alignment,x,y,average,i,pattern;
    for(pattern=0;pattern<3;++pattern) {
        for(i=0;i<sizeof(src);++i)
            src[i]=pattern==0?0:pattern==1?255:(uint8_t)((i*73+(i>>3)*19)^0xa5);
        for(alignment=0;alignment<16;++alignment)
        for(y=0;y<8;++y) for(x=0;x<8;++x) for(average=0;average<2;++average) {
            for(i=0;i<sizeof(actual);++i) actual[i]=expected[i]=(uint8_t)(i*37+91);
            if(average) avg_h264_chroma_mc8_8_c(expected+8,src+alignment,32,16,x,y);
            else put_h264_chroma_mc8_8_c(expected+8,src+alignment,32,16,x,y);
            xenon_chroma8(actual+8,src+alignment,32,16,x,y,average);
            if(memcmp(expected,actual,sizeof(actual))) return 0;
        }
    }
    return 1;
}

static void xenon_chroma_init(H264ChromaContext *c, int bit_depth)
{
    static volatile LONG checked=0;
    static int valid=0;
    if(bit_depth!=8) return;
    if(InterlockedCompareExchange(&checked,1,0)==0) {
        valid=xenon_chroma_check();
        av_log(NULL,valid?AV_LOG_INFO:AV_LOG_ERROR,
               "Xenon chroma VMX scalar comparison %s (6144 cases); enabled=%d\n",
               valid?"passed":"FAILED",valid);
        InterlockedExchange(&checked,2);
    } else while(InterlockedCompareExchange(&checked,2,2)!=2) Sleep(1);
    if(valid) {
        c->put_h264_chroma_pixels_tab[0]=xenon_put_chroma8;
        c->avg_h264_chroma_pixels_tab[0]=xenon_avg_chroma8;
    }
}
