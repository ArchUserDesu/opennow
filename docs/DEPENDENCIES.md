# Xenon dependency set

`Makefile.xenon` expects a Xenon-targeted prefix at `third_party/xenon-prefix` unless `XENON_PORTLIBS` is overridden. It must contain headers and static libraries for:

- FFmpeg: `libavcodec`, `libavutil` with H.264 decoder support.
- libopus for 48 kHz stereo GFN audio.
- libcurl with HTTPS, TLS and the WebSocket API (`curl_ws_send` / `curl_ws_recv`).
- jansson.
- OpenNOW's patched `libpeer`.
- libpeer dependencies: libsrtp2, usrsctp, mbedTLS (`mbedtls`, `mbedx509`, `mbedcrypto`) and cJSON.
- zlib as required by the surrounding stack.

The pinned upstream libpeer declares the same dependency family. When cross-building it for Xenon, audit endian defines carefully: Xenon is PowerPC big-endian and an upstream hard-coded `__LITTLE_ENDIAN` must not be carried over blindly.

For FFmpeg, start with a minimal cross-build that enables H.264 decoding and disables unrelated encoders, muxers, programs and devices. For curl, make sure your TLS backend and CA trust path work in the LibXenon/newlib environment and that the curl version includes its WebSocket API.

The audio path uses LibXenon's `xenon_sound_init()` / `xenon_sound_submit()`. The driver expects 48 kHz signed 16-bit stereo PCM in little-endian byte order; the port explicitly serializes the decoded samples to little-endian before submission because Xenon itself is big-endian.
