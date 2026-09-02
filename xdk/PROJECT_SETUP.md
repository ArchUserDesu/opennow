# OpenNOW XEX build

This folder is the dashboard-native Xbox 360 project. It does not use
XellLaunch/XeLL at runtime.

## Requirements

- an Xbox 360 XDK/Visual Studio environment you are authorized to use;
- Git;
- Python 3;
- PowerShell;
- MSBuild with the `Xbox 360` platform installed.

The build scripts do not include or redistribute the proprietary XDK.


## Recommended one-command build (works without Xbox MSBuild integration)

For older XDK installs where the Xbox Visual Studio/MSBuild integration is absent, use the direct toolchain driver. From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\xdk\build-port.ps1 -Configuration Release -XdkRoot "C:\Program Files (x86)\Microsoft Xbox 360 SDK"
```

`build-port.ps1` uses the bundled pinned sources when they are present (so the archive builds offline), applies **all retained dependency overrides**, regenerates the static-library projects, then invokes `cl.exe`, `lib.exe`, `link.exe`, and `imagexex.exe` directly. Use `-RefreshDeps` only when you intentionally want to reset/refetch the pinned dependency revisions. `-SkipFetch` remains available to force an offline-only build and fail if a dependency tree is missing. The `-XdkRoot` argument may point at another XDK installation with the same standard `bin\win32`, `include\xbox`, and `lib\xbox` layout.

## Build

Open an Xbox 360 XDK Visual Studio command prompt, then from this folder run:

```powershell
.\build.ps1 -Configuration Release
```

The script:

1. fetches pinned dependency source;
2. patches libpeer's small POSIX/Xbox differences;
3. generates Xbox 360 static-library projects for cJSON, Jansson, Opus,
   mbedTLS, libsrtp and libpeer;
4. builds XBMC-360's Xbox 360 FFmpeg `libavutil` and `libavcodec` projects;
5. builds `OpenNOW-XEX.vcxproj`;
6. copies the CA bundle beside the XEX.

After the first fetch you can rebuild without network access using:

```powershell
.\build.ps1 -Configuration Release -SkipFetch
```

Expected runtime folder:

```text
xdk\bin\Release\
    default.xex
    cacert.pem
```

Copy both files into one folder on `Hdd1:` and launch `default.xex` from your
normal dashboard/homebrew launcher.

## Direct Xbox libraries

The application links the normal Xbox title libraries used by the platform
layer:

```text
xnet.lib
xapilib.lib
d3d9.lib
xaudio2.lib
```

No D3DX font library is required; the UI font is built into
`source/platform/xdk_platform.cpp`.

## Networking / Wi-Fi

The XEX calls `XNetStartup`, `WSAStartup`, then waits for
`XNetGetTitleXnAddr`. Outbound HTTPS/WSS and WebRTC sockets are regular Xbox
Winsock sockets. This means the program uses the network interface managed by
the Xbox kernel, including a Wi-Fi connection already configured for the
console, instead of needing a bare-metal wireless driver.

## Decoder

H.264 is software decoded. The dependency build uses the Xbox/MSVC FFmpeg port
from XBMC-360 and links `libavcodec`/`libavutil`. Direct3D only presents the
decoded output; it is not used as an H.264 hardware decoder.

## If the first XDK build reports errors

Save the full MSBuild output. SDK releases differ slightly in header/library
signatures, and those differences cannot be compiled against without the XDK.
The source is structured so such fixes should be confined to `xdk/compat/` or
`source/platform/xdk_*` rather than the GFN protocol implementation.
