#include "opennow/rtp_h264.hpp"
namespace opennow {
H264RtpDepacketizer::H264RtpDepacketizer():ts_(0),expected_(0),have_seq_(false),have_ts_(false),fu_open_(false),idr_(false),dropped_(0){}
void H264RtpDepacketizer::reset(){au_.clear();have_seq_=have_ts_=fu_open_=idr_=false;}
void H264RtpDepacketizer::add_start_code(){static const std::uint8_t sc[]={0,0,0,1};au_.insert(au_.end(),sc,sc+4);}
void H264RtpDepacketizer::add_nal(const std::uint8_t* p,std::size_t n){if(!n)return;add_start_code();au_.insert(au_.end(),p,p+n);if((p[0]&0x1f)==5)idr_=true;}
bool H264RtpDepacketizer::flush(H264AccessUnit& o){if(au_.empty())return false;o.timestamp=ts_;o.idr=idr_;o.annexb.swap(au_);idr_=false;fu_open_=false;return true;}
bool H264RtpDepacketizer::push(const RtpPacketView& p,H264AccessUnit& out){
 if(!p.payload||p.payload_size<1)return false;
 if(have_seq_&&p.sequence!=expected_){++dropped_;au_.clear();fu_open_=false;idr_=false;} expected_=std::uint16_t(p.sequence+1);have_seq_=true;
 if(!have_ts_){ts_=p.timestamp;have_ts_=true;} else if(p.timestamp!=ts_){ if(!au_.empty()){++dropped_;au_.clear();} ts_=p.timestamp;fu_open_=false;idr_=false; }
 const std::uint8_t nal=p.payload[0],type=nal&0x1f;
 if(type>=1&&type<=23){add_nal(p.payload,p.payload_size);}
 else if(type==24){std::size_t off=1;while(off+2<=p.payload_size){std::size_t n=(std::size_t(p.payload[off])<<8)|p.payload[off+1];off+=2;if(n==0||off+n>p.payload_size){++dropped_;au_.clear();return false;}add_nal(p.payload+off,n);off+=n;}}
 else if(type==28){if(p.payload_size<2){++dropped_;return false;}const std::uint8_t ind=p.payload[0],hdr=p.payload[1];bool start=hdr&0x80,end=hdr&0x40;std::uint8_t rebuilt=(ind&0xe0)|(hdr&0x1f);if(start){add_start_code();au_.push_back(rebuilt);au_.insert(au_.end(),p.payload+2,p.payload+p.payload_size);fu_open_=true;if((rebuilt&0x1f)==5)idr_=true;}else if(fu_open_){au_.insert(au_.end(),p.payload+2,p.payload+p.payload_size);}else{++dropped_;return false;}if(end)fu_open_=false;}
 else {++dropped_;return false;}
 return p.marker?flush(out):false;
}
}
