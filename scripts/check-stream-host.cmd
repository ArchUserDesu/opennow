@echo off
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "STREAM_VS=%%i"
if not defined STREAM_VS exit /b 1
call "%STREAM_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist xdk\obj\host mkdir xdk\obj\host
cl /nologo /EHsc /W4 /Iinclude scripts\check-rtcp-receiver.cpp source\webrtc\gfn_sdp.cpp xdk\deps-src\opennow-switch\extern\libpeer\src\rtcp.c /Foxdk\obj\host\ /Fexdk\obj\host\check-rtcp-receiver.exe /link ws2_32.lib
if errorlevel 1 exit /b 1
xdk\obj\host\check-rtcp-receiver.exe
exit /b %errorlevel%
