# OpenNOW-Switch -> OpenNOW-Xenon mapping

| OpenNOW concern | Xenon implementation |
|---|---|
| GFN auth/catalog/CloudMatch | `source/gfn/gfn_client.cpp` |
| auth persistence | `source/gfn/persistence.cpp` |
| HTTP | `source/net/http_client.cpp` using libcurl |
| WebSocket signaling | `source/net/websocket.cpp` using libcurl WebSocket API |
| GFN SDP/NVST | `source/webrtc/gfn_sdp.cpp` |
| WebRTC/media/input session | `source/webrtc/webrtc_session.cpp` + libpeer |
| H.264 backend | `source/media/ffmpeg_h264_decoder.cpp`, CPU only |
| Opus backend | `source/media/opus_audio_decoder.cpp` |
| Switch NVDEC/Deko3D | replaced by CPU FFmpeg + Xenos framebuffer |
| Switch audio | replaced by `xenon_sound` 48 kHz stereo PCM |
| Switch controller | replaced by LibXenon `get_controller_data()` |
| Borealis | replaced by native text/controller launcher |

The code is a platform port, not a line-for-line UI port. Protocol behavior is kept close to the pinned OpenNOW-Switch revision while the Switch-specific rendering/input/UI stack is removed.
