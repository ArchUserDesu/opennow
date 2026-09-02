# End-to-end integration map

The integration is already present in the tree. This document is a debugging map, not a list of missing application stages.

```text
source/app/main_xenon.cpp
  |-- source/gfn/gfn_client.cpp
  |     |-- provider discovery
  |     |-- QR/device OAuth
  |     |-- token refresh
  |     |-- authenticated GraphQL catalog
  |     `-- CloudMatch start/poll/stop
  |-- source/net/http_client.cpp       (curl HTTPS)
  |-- source/net/websocket.cpp         (curl WebSocket)
  |-- source/webrtc/gfn_sdp.cpp        (GFN SDP/NVST adaptation)
  `-- source/webrtc/webrtc_session.cpp
        |-- libpeer ICE/DTLS/SRTP/SCTP
        |-- GFN signaling protocol
        |-- H.264 -> FFmpeg CPU decoder
        |-- Opus -> xenon_sound
        `-- controller -> GFN input_channel_v1
```

`source/core/rtp_h264.cpp` remains as a tested RTP/H.264 utility, but the normal libpeer integration does not depacketize twice: libpeer's `PeerVideoPacket` callback supplies the encoded H.264 access unit directly to the FFmpeg decoder.

The first optimization target after a successful hardware run should be decode/presentation time. Keep queues shallow; for cloud gaming, dropping stale work is preferable to accumulating several frames of latency.
