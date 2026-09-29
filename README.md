# OpenNOW for Xbox 360

An unofficial GeForce NOW client for RGH/JTAG Xbox 360 consoles. OpenNOW provides QR sign-in, game-library browsing, cloud-session launch, controller input, Opus audio, and software H.264 video decoding in a dashboard-native XEX.

The current profile targets **1280×720 at 30 FPS** with an adaptive bitrate up to 4 Mbps. Video decoding is CPU-based; Direct3D 9 handles YUV presentation.

## Requirements

- RGH/JTAG Xbox 360
- Xbox 360 XDK installed on Windows
- PowerShell and Python 3
- An active GeForce NOW account

The proprietary Xbox 360 XDK is not included.

## Build

From the repository root:

```bat
BUILD_XEX.bat "C:\Program Files (x86)\Microsoft Xbox 360 SDK"
```

Or call the build driver directly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\xdk\build-port.ps1 -Configuration Release -XdkRoot "C:\Program Files (x86)\Microsoft Xbox 360 SDK"
```

The pinned dependency sources are included and the normal build is offline-capable. Output is written to `xdk\bin\Release\`.

## Install

1. Download `OpenNOW-Xbox360.zip` from Releases.
2. Extract `default.xex` and `cacert.pem` into the same folder on the console.
3. Launch `default.xex` from Aurora, XeXMenu, or another homebrew launcher.
4. Follow the QR sign-in flow and select a game.

During streaming, press **LB + RB + Y** to open the OpenNOW menu. The menu provides input-mode switching, an on-screen keyboard, the GeForce NOW overlay, and session exit.

## Notes

- Ethernet or console-configured Wi-Fi is supported through XNet/Winsock.
- Authentication data is stored locally beside the application.
- NVIDIA may change its private streaming APIs without notice.
- OpenNOW is not affiliated with or endorsed by NVIDIA or Microsoft.

## Credits

Based on [OpenNOW for Nintendo Switch](https://github.com/OpenCloudGaming/OpenNOW) and the Xbox 360 FFmpeg work from XBMC-360. Third-party components retain their respective licenses.
