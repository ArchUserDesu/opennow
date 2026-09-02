param(
  [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
  [switch]$SkipFetch
)
$ErrorActionPreference = 'Stop'
$here = (Resolve-Path $PSScriptRoot).Path
$deps = Join-Path $here 'deps-src'

if (!$env:XEDK -and !$env:XDK) {
  Write-Warning 'XEDK/XDK environment variable is not set. Run this from an Xbox 360 XDK/Visual Studio command prompt.'
}
if (!(Get-Command msbuild.exe -ErrorAction SilentlyContinue)) {
  throw 'msbuild.exe not found. Open an Xbox 360 XDK Visual Studio command prompt.'
}
if (!(Get-Command python.exe -ErrorAction SilentlyContinue)) {
  throw 'python.exe not found (Python 3 is used only to generate dependency project files).'
}

if (!$SkipFetch) { & "$here\fetch-deps.ps1" -Destination $deps }
python "$here\patch-deps.py" --src $deps
python "$here\apply-overrides.py" --src $deps
python "$here\generate-deps-projects.py" --src $deps

$msb = @('/m','/t:Build',"/p:Configuration=$Configuration",'/p:Platform=Xbox 360')
function BuildProject($p) {
  Write-Host "==> $p"
  & msbuild.exe $p @msb
  if ($LASTEXITCODE -ne 0) { throw "Build failed: $p" }
}

# Pure C dependencies.
BuildProject "$here\projects\cjson.vcxproj"
BuildProject "$here\projects\jansson.vcxproj"
BuildProject "$here\projects\opus.vcxproj"
BuildProject "$here\projects\mbedtls_opennow.vcxproj"
BuildProject "$here\projects\srtp2.vcxproj"
BuildProject "$here\projects\peer.vcxproj"

# Software H.264 decoder: use the known Xbox 360/MSVC FFmpeg port from XBMC-360.
$ff = "$deps\xbmc360\libraries\ffmpeg\vcproj"
BuildProject "$ff\libavutil\libavutil.vcxproj"
BuildProject "$ff\libavcodec\libavcodec.vcxproj"

# Main XEX.
BuildProject "$here\OpenNOW-XEX.vcxproj"

$out = "$here\bin\$Configuration"
$caSrc = Join-Path $here 'cacert.pem'
if (!(Test-Path $caSrc)) { throw 'cacert.pem missing; rerun fetch-deps.ps1 or copy a Mozilla CA bundle to xdk\cacert.pem' }
Copy-Item -Force $caSrc "$out\cacert.pem"
Write-Host ""
Write-Host "Build output: $out\default.xex"
Write-Host "Runtime files: default.xex + cacert.pem"
