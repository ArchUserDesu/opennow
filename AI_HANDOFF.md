# OpenNOW Xbox 360 XEX build handoff

## Goal and environment

Produce a dashboard-native Xbox 360 `default.xex` for RGH/JTAG from this project. The VM has the full Xbox 360 SDK at:

`C:\Program Files (x86)\Microsoft Xbox 360 SDK`

The installed compiler is the old PowerPC XDK MSVC compiler (`cl.exe` 16.00.11886). Visual Studio Xbox MSBuild integration is absent, so use `xdk\build-direct.ps1`; it directly calls `cl.exe`, `lib.exe`, `link.exe`, and `imagexex.exe`.

Do not switch to libxenon/nxdk: they do not produce a dashboard-native XDK XEX. Keep the Xbox-specific FFmpeg already pinned from XBMC-360.

## Restore a fresh handoff

1. Extract this archive to a writable directory.
2. From its project root run:

   `powershell -NoProfile -ExecutionPolicy Bypass -File .\xdk\fetch-deps.ps1`

   This checks out the exact pinned dependency revisions into `xdk\deps-src`.
3. Copy every file under `dependency-overrides` over the matching path under `xdk\deps-src`. These are the compatibility edits already completed. PowerShell example from the extracted project root:

   `Copy-Item -Recurse -Force .\dependency-overrides\* .\xdk\deps-src\`

4. Run:

   `python .\xdk\generate-deps-projects.py`

5. Apply the small libsrtp project-generator correction described below before starting the build.

If continuing in the original VM/worktree instead of a fresh extraction, dependencies, all overrides, four completed libraries, and incremental objects are already present. Do not refetch or clean them.

## Completed work

- Installed the XDK's full toolchain after discovering the first install was partial.
- Installed Python 3.12 and fetched all dependencies at their pinned revisions.
- Added a direct XDK build driver: `xdk\build-direct.ps1`.
- Successfully compiled and archived:
  - `cjson.lib`
  - `jansson.lib`
  - `opus.lib`
  - `mbedtls_opennow.lib`
- Ported current mbedTLS to the old compiler by compiling its C files as C++, adding required pointer/enum casts, removing unsupported modules, fixing old-C++ parser issues, and using Xbox headers where desktop Windows headers were selected incorrectly.
- Preserved TLS 1.2/DTLS, AES-GCM, ECDHE-RSA/ECDHE-ECDSA, SHA-256, X.509, and DTLS-SRTP. TLS 1.3, PSA, hardware crypto for non-Xenon CPUs, and unused algorithms were disabled in `xdk\compat\mbedtls_xdk_config.h`.
- Confirmed XBMC-360 contains Xbox 360 XDK `.vcxproj` files for FFmpeg. It is pinned at commit `80a841fe1c8a88c8174eb52d28cc9c330bef76dc`; use it instead of porting modern FFmpeg.

The included `xdk\lib\Release` libraries are optional build accelerators. A clean build can recreate them.

## Exact current stopping point

Running:

`powershell -NoProfile -ExecutionPolicy Bypass -File .\xdk\build-direct.ps1`

now reaches `srtp2`. `aes.c` compiles, then `aes_gcm_mbedtls.c` fails because generated `srtp2.vcxproj` incorrectly includes every optional crypto backend.

Fix `xdk\generate-deps-projects.py` near the `srtp_sources` assignment. Exclude these files from the source list:

- `*_mbedtls.c`
- `*_nss.c`
- `*_ossl.c`

The intended built-in libsrtp backend is `aes.c`, `aes_icm.c`, `sha1.c`, and `hmac.c`. The config deliberately leaves OpenSSL/NSS/GCM backend macros undefined. After changing the generator, regenerate the projects. Alternatively add `ExcludedFromBuild` entries to `xdk\projects\srtp2.vcxproj`, but fixing the generator is durable.

`crypto/include/datatypes.h` was already fixed so `_XBOX` uses the `winsockx.h` definitions supplied by `xdk\compat\opennow_xdk_compat.h`.

## Remaining build sequence

1. Complete `srtp2.lib`.
   - Expect the old C compiler to reject declarations after statements or other C99 syntax. Make minimal source-compatible declaration moves, as was done for Opus, or compile only the individual troublesome source as C++ and add explicit casts.
   - Preserve `WORDS_BIGENDIAN=1`; Xenon is big-endian.
2. Complete `peer.lib`.
   - It is configured with `CONFIG_USE_USRSCTP=0`, `DISABLE_PEER_SIGNALING=1`, and `CONFIG_IPV6=0`.
   - `ports.c` and `sctp.c` already contain Xbox compatibility edits in the override set.
   - Do not change packet byte order or re-depacketize RTP; the application pipeline already expects libpeer's output.
3. Build XBMC-360 `libavutil.lib` and `libavcodec.lib`.
   - These are old XDK-specific FFmpeg projects, so they should require far fewer changes than modern dependencies.
   - Only H.264 CPU decode and its actual transitive libavcodec requirements are needed. If the generated project over-includes unrelated codecs/backends, exclude unused sources rather than porting all of FFmpeg.
   - Preserve Xbox/PowerPC configuration and big-endian behavior. Do not replace with desktop or libxenon binaries.
4. Compile the project sources listed in `xdk\OpenNOW-XEX.vcxproj`.
5. Link `default.pe`, convert it using `imagexex.exe`, and copy `cacert.pem`. The intended result is:

   `xdk\bin\Release\default.xex`

6. If final link errors occur:
   - Add `xboxkrnl.lib` if XDK runtime/kernel imports are unresolved.
   - Verify the old FFmpeg project's per-file compile settings; `build-direct.ps1` currently reads project-wide settings and `ExcludedFromBuild`, but not arbitrary per-file definitions/options.
   - Add only libraries demanded by actual unresolved symbols.
7. Validate without claiming hardware runtime testing:
   - Confirm `default.xex` exists and is non-empty.
   - Run `imagexex.exe /DUMP xdk\bin\Release\default.xex` and confirm it parses as an XEX.
   - Ensure `cacert.pem` is beside it.
   - Actual networking, video, audio, and dashboard launch still require an RGH/JTAG console test.

## Important constraints

- Target 1280x720 at 60 Hz.
- Keep CPU H.264 decoding and 48 kHz audio behavior.
- Preserve networking/endian conversions carefully.
- Do not treat host builds/tests as Xbox hardware validation.
- Do not clean `xdk\obj\direct` in the original VM; incremental objects save substantial rebuild time.
- Warnings `LNK4221` for disabled mbedTLS modules are expected and harmless.

## Dependency revisions

- `https://github.com/OpenCloudGaming/OpenNOW-Switch.git` at `dce9743f183a5c211bd5971d02993e8b7253cf4d`
- `https://github.com/akheron/jansson.git` at `ed5cae4ed0621ef409510f94270c9f8f263736d0`
- `https://github.com/xiph/opus.git` at `ddbe48383984d56acd9e1ab6a090c54ca6b735a6`
- `https://github.com/brentdc-nz/XBMC-360.git` at `80a841fe1c8a88c8174eb52d28cc9c330bef76dc`

## Build-driver notes

`xdk\build-direct.ps1` already:

- puts Xbox headers before Win32 headers;
- creates unique numbered object names;
- skips up-to-date objects;
- compiles mbedTLS as C++;
- parses project include paths, preprocessor definitions, forced includes, sources, and exclusions;
- creates libraries, links the PE, invokes `imagexex`, and stages the CA bundle.

Continue from the first compiler error each run. The completed objects are retained, so each iteration advances rather than rebuilding everything.
