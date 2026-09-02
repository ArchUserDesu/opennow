@echo off
setlocal
set "XDKROOT=%~1"
if "%XDKROOT%"=="" set "XDKROOT=C:\Program Files (x86)\Microsoft Xbox 360 SDK"

echo Building OpenNOW for Xbox 360...
echo XDK: %XDKROOT%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0xdk\build-port.ps1" -Configuration Release -XdkRoot "%XDKROOT%"
if errorlevel 1 (
  echo.
  echo BUILD FAILED.
  exit /b 1
)
echo.
echo BUILD COMPLETE: xdk\bin\Release\default.xex
endlocal
