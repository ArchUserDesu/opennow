# Xbox-kernel / XEX port

This tree contains two Xbox 360 targets:

- the original Free60/LibXenon bare-metal ELF target; and
- a dashboard-native Xbox 360 XDK/XEX target under `xdk/`.

The XEX target defines `OPENNOW_XDK=1` and keeps H.264 decoding software-only.
It is a source port, not an ELF-to-XEX wrapper.

## XEX runtime architecture

```text
Dashboard / Aurora / XeXMenu
        -> default.xex
        -> Xbox kernel networking (configured Ethernet or Wi-Fi)
        -> XNet + Winsock
        -> mbedTLS HTTPS / WSS
        -> NVIDIA auth + catalog + CloudMatch
        -> libpeer ICE / DTLS / SRTP / built-in SCTP
             -> H.264 -> FFmpeg CPU decoder -> YUV -> CPU ARGB -> D3D9
             -> Opus -> libopus -> XAudio2
             <- XInput controller -> GFN input_channel_v1
```

## Implemented XEX platform pieces

- XNet + Winsock startup and title-address wait. The application uses the Xbox
  kernel-managed network interface, so it does not need a LibXenon Wi-Fi driver.
- HTTPS: custom mbedTLS transport over Xbox Winsock.
- WebSocket signaling: RFC 6455 client over the same mbedTLS/Winsock transport;
  curl WebSocket support is not required by the XEX target.
- Controller input: XInput.
- Audio: XAudio2, 48 kHz stereo 16-bit PCM.
- Video presentation: Direct3D 9 ARGB texture upload with aspect-ratio scaling.
- UI: built-in 5x7 bitmap font rendered directly through Direct3D. No system
  font or D3DX font service is required.
- Storage: `game:\\opennow_session.json`, `game:\\opennow.log`, and
  `game:\\cacert.pem`.
- TLS/WebRTC entropy: `XeCryptRandom`.
- libpeer local address selection: `gethostid()` is backed by
  `XNetGetTitleXnAddr`, avoiding POSIX `getifaddrs()`.

## H.264 policy

H.264 remains CPU/software-only as requested. The XEX dependency scripts use
XBMC-360's Xbox 360/MSVC FFmpeg project as the decoder base. That project has a
PowerPC/Xbox 360 H.264 software decoder and Xbox-specific CPU/VMX code. OpenNOW
links only `libavcodec` + `libavutil` for the current decoder path.

The application still converts decoded YUV/NV12 frames to ARGB on the CPU, then
uploads the result to Direct3D. GPU YUV conversion can be a later optimization;
it is deliberately not part of this port.

## Dependency set for XEX

`xdk/fetch-deps.ps1` and `xdk/generate-deps-projects.py` prepare the source and
Xbox 360 static-library projects. The XEX target uses:

- XBMC-360 FFmpeg: `libavcodec`, `libavutil`
- Opus
- Jansson
- cJSON
- mbedTLS
- libsrtp2
- OpenNOW's pinned libpeer

The XEX target intentionally does **not** require:

- LibXenon
- curl
- usrsctp
- D3DX font rendering

libpeer is built with `CONFIG_USE_USRSCTP=0` so its internal SCTP implementation
is used. Its optional HTTP/MQTT signaling implementation is excluded because
OpenNOW already supplies NVIDIA signaling.

## Source files

The XEX application project compiles all normal OpenNOW application/core/GFN/
media/network/WebRTC sources plus:

- `source/platform/logger.cpp`
- `source/platform/xdk_platform.cpp`
- `source/platform/xdk_runtime.cpp`

It does not compile the LibXenon-only platform/runtime files.

## Compiler compatibility

The XEX source has been adjusted for the VS2010-era Xbox 360 toolchain where it
matters most: no `nullptr`, no `constexpr`, no range-for in XEX-compiled paths,
and no brace-value initialization in the main application path. The login API
continues to use `std::function`/lambdas, features supported by the VS2010 C++
standard library/compiler family used by the later Xbox 360 XDKs.

The dependency projects are C projects and force-include
`xdk/compat/opennow_xdk_compat.h` for the small POSIX compatibility surface.
libsrtp gets a dedicated big-endian PowerPC `config.h` and uses its built-in
AES/SHA1 implementation rather than OpenSSL.

## Bugs fixed while porting

- Session-file writes check stream state after `flush()`.
- `poll_session()`/`stop_session()` no longer call `.back()` on an empty service
  URL and use the same CloudMatch fallback as session creation.
- FFmpeg decoder reopen/failure paths release old contexts/frames correctly.
- Thumbstick normalization handles `-32768` correctly.
- XEX signaling no longer depends on curl WebSocket support.
- XEX libpeer no longer depends on POSIX interface enumeration or usrsctp.

## Validation possible in this environment

The portable protocol/video-conversion core is built with host CMake using
`OPENNOW_HOST_TESTS=OFF` because the small upload intentionally omitted the
`tests/` directory. Python dependency-project generators are syntax checked.

The proprietary Xbox 360 XDK is not installed here, therefore `default.xex`
cannot be produced or hardware-validated here. The first build with a real XDK
may expose SDK-version-specific symbol/signature differences; those are the only
class of errors that cannot be tested in this environment.
