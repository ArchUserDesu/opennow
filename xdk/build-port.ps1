param(
  [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
  [string]$XdkRoot = ''
)
$ErrorActionPreference = 'Stop'
$here = (Resolve-Path $PSScriptRoot).Path
$deps = Join-Path $here 'deps-src'

if (!$XdkRoot) {
  if ($env:XEDK) { $XdkRoot = $env:XEDK }
  elseif ($env:XDK) { $XdkRoot = $env:XDK }
  else {
    $default = 'C:\Program Files (x86)\Microsoft Xbox 360 SDK'
    if (Test-Path $default) { $XdkRoot = $default }
  }
}
if (!$XdkRoot -or !(Test-Path $XdkRoot)) {
  throw 'Xbox 360 XDK root not found. Pass -XdkRoot "C:\path\to\Microsoft Xbox 360 SDK".'
}
$XdkRoot = (Resolve-Path $XdkRoot).Path

function Need([string]$Path, [string]$What) {
  if (!(Test-Path $Path)) { throw "Missing $What at $Path" }
}
Need (Join-Path $XdkRoot 'bin\win32\cl.exe') 'Xbox compiler'
Need (Join-Path $XdkRoot 'bin\win32\link.exe') 'Xbox linker'
Need (Join-Path $XdkRoot 'bin\win32\lib.exe') 'Xbox librarian'
Need (Join-Path $XdkRoot 'bin\win32\imagexex.exe') 'imagexex'
Need (Join-Path $XdkRoot 'include\xbox\xtl.h') 'Xbox headers'
Need (Join-Path $XdkRoot 'lib\xbox\xapilib.lib') 'Xbox import libraries'

if (!(Get-Command python.exe -ErrorAction SilentlyContinue) -and !(Get-Command python3.exe -ErrorAction SilentlyContinue)) {
  throw 'Python 3 is required to prepare/generate dependency projects.'
}
$python = if (Get-Command python.exe -ErrorAction SilentlyContinue) { 'python.exe' } else { 'python3.exe' }

$requiredDeps = @(
  'opennow-switch\extern\libpeer\src\peer.c',
  'opennow-switch\extern\libpeer\third_party\libsrtp\srtp\srtp.c',
  'opennow-switch\extern\libpeer\third_party\mbedtls\library\ssl_tls.c',
  'jansson\src\jansson.h',
  'opus\include\opus.h',
  'xbmc360\libraries\ffmpeg\config.h',
  'xbmc360\libraries\ffmpeg\libavcodec\avcodec.h',
  'xbmc360\libraries\ffmpeg\libavcodec\h264.c',
  'xbmc360\libraries\ffmpeg\libavcodec\xb_thread.c',
  'xbmc360\libraries\ffmpeg\vcproj\libavcodec\libavcodec.vcxproj',
  'xbmc360\libraries\ffmpeg\vcproj\libavutil\libavutil.vcxproj'
)
$missingDeps = @($requiredDeps | Where-Object { !(Test-Path (Join-Path $deps $_)) })
if ($missingDeps.Count -ne 0) {
  throw ('Checked-in xdk\deps-src is incomplete. This build consumes the committed dependency sources directly. Missing: ' + ($missingDeps -join ', '))
}
Write-Host 'Using checked-in XDK dependency sources directly.'

& $python (Join-Path $here 'apply-overrides.py') --src $deps
if ($LASTEXITCODE -ne 0) { throw 'apply-overrides.py failed.' }

# The bundled dependency tree already carries some XDK fixes. apply-overrides.py
# also inserts declarations for freshly fetched trees, which can make a second
# identical declaration when both paths meet. Normalize those exact duplicates
# before compiling so repeated CI runs are deterministic.
$peerConnection = Join-Path $deps 'opennow-switch\extern\libpeer\src\peer_connection.c'
if (Test-Path $peerConnection) {
  $pcText = [System.IO.File]::ReadAllText($peerConnection)
  while ($pcText.Contains("  int dtls_ret = 0;`r`n  int dtls_ret = 0;")) {
    $pcText = $pcText.Replace("  int dtls_ret = 0;`r`n  int dtls_ret = 0;", "  int dtls_ret = 0;")
  }
  while ($pcText.Contains("  int dtls_ret = 0;`n  int dtls_ret = 0;")) {
    $pcText = $pcText.Replace("  int dtls_ret = 0;`n  int dtls_ret = 0;", "  int dtls_ret = 0;")
  }
  [System.IO.File]::WriteAllText($peerConnection, $pcText)
}

& $python (Join-Path $here 'generate-deps-projects.py') --src $deps
if ($LASTEXITCODE -ne 0) { throw 'generate-deps-projects.py failed.' }

& (Join-Path $here 'build-direct.ps1') -Configuration $Configuration -XdkRoot $XdkRoot
if ($LASTEXITCODE -ne 0) { throw 'Direct XDK build failed.' }

$out = Join-Path $here "bin\$Configuration"
$xex = Join-Path $out 'default.xex'
$ca = Join-Path $out 'cacert.pem'
Need $xex 'built XEX'
Need $ca 'runtime CA bundle'
if ((Get-Item $xex).Length -le 0) { throw 'default.xex is empty.' }
Write-Host ''
Write-Host 'OpenNOW XEX build complete:'
Write-Host "  $xex"
Write-Host "  $ca"
