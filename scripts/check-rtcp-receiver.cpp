#include <cassert>
#include <cstdio>
#include "../xdk/deps-src/opennow-switch/extern/libpeer/src/rtcp_receiver.h"
#include "opennow/gfn_sdp.hpp"
extern "C" {
#include "../xdk/deps-src/opennow-switch/extern/libpeer/src/rtcp.h"
}

int main() {
    RtcpReceiver r = {};
    uint8_t packet[128];
    const uint8_t pli[]={0x81,206,0,2,1,2,3,4,0xaa,0xbb,0xcc,0xdd};
    const uint8_t nack[]={0x81,205,0,3,1,2,3,4,0xaa,0xbb,0xcc,0xdd,0xff,0xfe,0x80,1};
    assert(rtcp_get_pli(packet,12,0x01020304,0xaabbccdd)==12);
    assert(!memcmp(packet,pli,sizeof(pli)));
    assert(rtcp_get_nack(packet,16,0x01020304,0xaabbccdd,65534,0x8001)==16);
    assert(!memcmp(packet,nack,sizeof(nack)));
    assert(rtcp_get_nack(packet,15,1,2,3,4)==-1);
    rr_receive(&r,65534,0,0);
    rr_receive(&r,0,1800,20); // missing 65535, including sequence wrap
    assert(rr_build(&r,packet,0x01020304,0xaabbccdd,500)==52);
    assert(packet[0]==0x81 && packet[1]==201 && packet[3]==7);
    assert(rr_read32(packet+4)==0x01020304 && rr_read32(packet+8)==0xaabbccdd);
    assert(packet[12]==85 && (rr_read32(packet+12)&0xffffff)==1);
    assert(rr_read32(packet+16)==65536 && rr_read32(packet+20)==0);
    assert(packet[32]==0x81 && packet[33]==202 && packet[35]==4);
    assert(packet[41]==7 && !memcmp(packet+42,"opennow",7) && packet[49]==0);
    rr_receive(&r,65535,900,30); // late repair must not advance highest
    rr_build(&r,packet,1,2,1000);
    assert(rr_read32(packet+12)==0 && rr_read32(packet+16)==65536);
    rr_receive(&r,0,1800,40); // duplicates may produce signed negative loss
    rr_build(&r,packet,1,2,1500);
    assert(rr_read32(packet+12)==0x00ffffff);
    r.lsr=0x12345678;r.have_sr=1;r.sr_ms=0xffffff00u;
    rr_build(&r,packet,1,2,0x000002e8u); // clock wrap, one second
    assert(rr_read32(packet+24)==0x12345678 && rr_read32(packet+28)==65536);
    rr_receive(&r,20000,0,3000); // implausible jump needs confirmation
    assert(r.highest==65536);
    rr_receive(&r,20001,900,3010);
    assert(r.highest==20001 && r.received==1);

    opennow::StreamConfig cfg;
    cfg.bitrate_kbps=12000;
    opennow::RiInputCaps caps;
    std::string s=opennow::build_nvst_sdp("",cfg,caps);
    assert(s.find("a=vqos.dynamicStreamingMode:3\n")!=std::string::npos);
    assert(s.find("a=vqos.drc.enable:1\n")!=std::string::npos);
    assert(s.find("a=video.initialBitrateKbps:4000\n")!=std::string::npos);
    assert(s.find("a=vqos.bw.minimumBitrateKbps:4000\n")!=std::string::npos);
    assert(s.find("a=vqos.bw.maximumBitrateKbps:12000\n")!=std::string::npos);
    cfg.bitrate_kbps=2000;
    s=opennow::build_nvst_sdp("",cfg,caps);
    assert(s.find("a=vqos.bw.maximumBitrateKbps:4000\n")!=std::string::npos);
    puts("PASS: receiver-report wire format, loss, repair, duplicate, wrap, restart and dynamic SDP (host only)");
}
