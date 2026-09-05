#ifndef OPENNOW_RTCP_RECEIVER_H
#define OPENNOW_RTCP_RECEIVER_H
#include <stdint.h>
#include <string.h>

/* RFC 3550 receiver statistics, owned by the transport thread. All wire
 * access is explicit: Xbox compiler bitfield layout is not a wire format. */
typedef struct RtcpReceiver {
  uint32_t base, highest, received, expected_prior, received_prior;
  uint32_t transit, jitter, lsr, sr_ms, report_ms, bad_sequence;
  int initialized, have_sr;
} RtcpReceiver;

static uint32_t rr_read32(const uint8_t* p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void rr_write32(uint8_t* p, uint32_t v) {
  p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static void rr_receive(RtcpReceiver* r, uint16_t seq, uint32_t timestamp, uint32_t now) {
  uint32_t transit=now*90u-timestamp;
  if (!r->initialized) {
    r->base=r->highest=seq;r->initialized=1;r->transit=transit;
    r->report_ms=now;r->bad_sequence=65536u;
  } else {
    uint16_t delta=(uint16_t)(seq-(uint16_t)r->highest);
    if (delta<3000) r->highest+=delta;
    else if (delta<=65536u-100u) {
      if ((uint32_t)seq!=r->bad_sequence) {
        r->bad_sequence=(uint16_t)(seq+1);return;
      }
      /* Two consecutive packets confirm a sender sequence restart. */
      r->base=r->highest=seq;r->received=0;
      r->expected_prior=r->received_prior=0;r->jitter=0;
      r->transit=transit;r->bad_sequence=65536u;
    }
  }
  ++r->received;
  {
    int64_t d=(int32_t)(transit-r->transit);
    if(d<0)d=-d;
    r->transit=transit;
    /* Q4 jitter, avoiding signed overflow on timestamp discontinuities. */
    r->jitter=(uint32_t)((int64_t)r->jitter+d-((r->jitter+8ULL)>>4));
  }
}
static int rr_build(RtcpReceiver* r, uint8_t* out, uint32_t sender,
                    uint32_t media, uint32_t now) {
  uint32_t expected=r->highest-r->base+1;
  uint32_t interval=expected-r->expected_prior;
  int64_t lost=(int64_t)expected-r->received;
  int64_t interval_lost=(int64_t)interval-(uint32_t)(r->received-r->received_prior);
  uint32_t fraction=interval&&interval_lost>0?(uint32_t)(interval_lost*256/interval):0;
  if(fraction>255)fraction=255;
  if(lost>8388607)lost=8388607;
  if(lost<-8388608)lost=-8388608;
  memset(out,0,52);
  out[0]=0x81;out[1]=201;out[3]=7;
  rr_write32(out+4,sender);rr_write32(out+8,media);
  rr_write32(out+12,(fraction<<24)|((uint32_t)lost&0xffffff));
  rr_write32(out+16,r->highest);rr_write32(out+20,r->jitter>>4);
  rr_write32(out+24,r->lsr);
  rr_write32(out+28,r->have_sr?(uint32_t)((uint64_t)(now-r->sr_ms)*65536/1000):0);
  /* Compound RR + SDES CNAME, padded to a word boundary. */
  out[32]=0x81;out[33]=202;out[35]=4;rr_write32(out+36,sender);
  out[40]=1;out[41]=7;memcpy(out+42,"opennow",7);
  r->expected_prior=expected;r->received_prior=r->received;r->report_ms=now;
  return 52;
}
#endif
