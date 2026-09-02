# OpenNOW-Xenon

## Xbox 360 XDK quick build

This archive includes the pinned dependency source trees, including the XBMC-360 FFmpeg Xbox port used for software H.264 decoding. On a Windows machine with the Xbox 360 XDK installed, the recommended build is:

```bat
BUILD_XEX.bat "C:\Program Files (x86)\Microsoft Xbox 360 SDK"
```

Or run the PowerShell driver directly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\xdk\build-port.ps1 -Configuration Release -XdkRoot "C:\Program Files (x86)\Microsoft Xbox 360 SDK"
```

The bundled dependency trees are used automatically, so the normal build is offline-capable. Use `-RefreshDeps` only if you intentionally want to reset/refetch the pinned upstream revisions. Output is `xdk\bin\Release\default.xex` plus `cacert.pem`.


Native GeForce NOW client port for Xbox 360 homebrew using Free60/LibXenon. This tree contains the end-to-end client flow rather than a platform-only scaffold: NVIDIA device/QR authentication, token refresh and persistence, authenticated GFN catalog browsing, CloudMatch session launch/poll/stop, NVIDIA signaling over WebSocket, libpeer WebRTC/ICE/DTLS/SRTP/SCTP, GFN controller input, CPU H.264 decode through FFmpeg, Opus audio decode, Xbox audio output, and Xenos framebuffer presentation.

The initial target is intentionally conservative: **1280x720 @ 60 FPS, H.264 software decoding on Xenon CPU**. There is no assumed Xenos/NVDEC-style hardware video decoder.

## Status

The full Xenon target and all required PowerPC libraries now build and link. The
dashboard-ready release contains a retail `default.xex` launcher, the stripped
LibXenon `xenon.elf`, and its TLS CA bundle. The portable tests pass and the
final OpenNOW executable has no unresolved symbols.

An Xbox 360 is still required for live hardware validation. In particular,
NVIDIA can change its private service protocol, and sustained 720p60 software
H.264 performance cannot be measured in this VM.

## Runtime flow

```text
LibXenon network
  -> NVIDIA QR/device login + saved token
  -> authenticated GFN catalog
  -> CloudMatch v2 session
  -> NVIDIA WebSocket signaling
  -> libpeer ICE / DTLS / SRTP / SCTP
       -> H.264 access units -> FFmpeg CPU decode -> YUV->ARGB -> Xenos framebuffer
       -> Opus packets       -> libopus decode       -> xenon_sound 48k stereo
       <- Xbox 360 pad       <- GFN input_channel_v1 / SID 0
```

The console UI is deliberately text/controller based rather than a Borealis UI port. D-pad navigates, A confirms, B cancels; during streaming BACK+START exits.

## Dependencies

Install Xenon-targeted headers and static libraries under `third_party/xenon-prefix` (or set `XENON_PORTLIBS`). The console target expects FFmpeg (`avcodec`, `avutil`), libopus, libcurl with WebSocket/TLS support, jansson, OpenNOW's libpeer, and libpeer's `srtp2`, `usrsctp`, `mbedtls`, `mbedx509`, `mbedcrypto`, and `cjson` dependencies. See `docs/DEPENDENCIES.md`.

## Build

```sh
export DEVKITXENON=/path/to/devkitxenon
export PATH="$DEVKITXENON/bin:$PATH"
make check-xenon-env
make xenon
```

The source uses exceptions in the service/auth path, so the C++ runtime used by the Xenon toolchain must provide working exception support. `Makefile.xenon` intentionally does not use `-fno-exceptions`.

`Makefile.xenon` configures curl to find `cacert.pem` in the working directory
or at the root of common LibXenon storage devices. The release package includes
the bundle in the required location.

## Install on Xbox 360

Use `dist/OpenNOW-Xenon-XEX.zip`. Extract its **contents directly to the root**
of a FAT32 USB drive so that `/default.xex`, `/xenon.elf`, and `/cacert.pem`
exist. In Aurora or XeXMenu, launch `default.xex`. It starts embedded XeLL,
which finds and launches `xenon.elf` automatically.

`default.xex` is XellLaunch2 v2.2.0 retail. It is a dashboard-compatible bridge
to the bare-metal LibXenon application, not a renamed or ABI-incompatible ELF.
Do not rename, move, or remove `xenon.elf`. Ethernet with DHCP is expected.
D-pad navigates, A confirms, B cancels, and BACK+START exits a stream.

## Dashboard-native XEX target

This tree also contains a direct Xbox-kernel/XDK port under `xdk/`. Unlike the
LibXenon package above, this target builds a real `default.xex`, uses XNet/
Winsock (including the console's configured Wi-Fi), XInput, XAudio2 and
Direct3D 9, and does not launch XeLL. H.264 remains software-decoded through
FFmpeg. See `xdk/PROJECT_SETUP.md` and `docs/XEX_PORT.md`.

## Portable checks

The top-level CMake build only tests architecture-independent code and is not a substitute for a Xenon build:

```sh
make host-test
```

## Upstream reference

The implementation was ported against OpenCloudGaming/OpenNOW-Switch commit `dce9743f183a5c211bd5971d02993e8b7253cf4d`. `scripts/fetch-upstream.sh` is retained only as a convenient reference checkout; the Xbox client does **not** require copying additional application modules from that checkout before it is logically complete.

See `docs/PORT_MAP.md` for the source mapping and `docs/BUILD_STATUS.md` for exactly what remains unverified.
