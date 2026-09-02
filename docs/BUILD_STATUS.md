# Build and validation status

## Implemented in this source tree

- LibXenon initialization, lwIP polling, controller polling and Xenos framebuffer presentation.
- NVIDIA device/QR auth, refresh-token handling, saved-session persistence and provider discovery.
- Authenticated GeForce NOW GraphQL catalog with launch-variant selection.
- CloudMatch launch, polling and stop requests with GFN WebRTC session metadata parsing.
- Secure WebSocket signaling using libcurl's WebSocket API.
- GFN offer/answer adaptation, NVST SDP, ICE trickle/fallback candidate handling and libpeer transport setup.
- GFN reliable input data channel and Xbox 360 controller reports with explicit endian serialization.
- CPU-only FFmpeg H.264 decode and Xenos presentation.
- Opus stereo decode and LibXenon `xenon_sound` 48 kHz PCM output.

## Not verified in the creation environment

There is no `xenon-g++`/devkitXenon installation, no Xbox 360 hardware, and no Xenon-targeted builds of FFmpeg/curl/jansson/libpeer/libopus here. Consequently this package is **source-complete but not build- or hardware-certified**. Real-world fixes may be needed for third-party configure scripts, LibXenon/newlib behavior, TLS certificate storage, libpeer big-endian assumptions, framebuffer pitch/format behavior, audio buffering, and CPU performance.

The portable endian/input/RTP/YUV tests can be run on a host with `make host-test`.
