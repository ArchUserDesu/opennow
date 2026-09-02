param(
  [string]$Destination = "$PSScriptRoot\deps-src"
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $PSScriptRoot).Path
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

function Checkout($url, $dir, $ref, $recursive=$false) {
  if (!(Test-Path $dir)) {
    $args = @('clone','--filter=blob:none')
    if ($recursive) { $args += '--recursive' }
    $args += @($url,$dir)
    & git @args
  }
  Push-Location $dir
  git fetch --tags --force origin
  git checkout --force $ref
  if ($recursive) { git submodule update --init --recursive }
  Pop-Location
}

# Proven Xbox 360 XDK FFmpeg project (software H.264 + PPC/VMX CPU paths).
Checkout 'https://github.com/brentdc-nz/XBMC-360.git' "$Destination\xbmc360" '80a841fe1c8a88c8174eb52d28cc9c330bef76dc'

# Exact upstream revision OpenNOW-Xenon was ported against; its libpeer gitlink
# pins libpeer and all nested crypto/transport dependencies.
Checkout 'https://github.com/OpenCloudGaming/OpenNOW-Switch.git' "$Destination\opennow-switch" 'dce9743f183a5c211bd5971d02993e8b7253cf4d' $true

# Standalone application dependencies. Their APIs used here are stable C APIs.
Checkout 'https://github.com/akheron/jansson.git' "$Destination\jansson" 'v2.14.1'
Checkout 'https://github.com/xiph/opus.git' "$Destination\opus" 'v1.5.2'

Write-Host "Dependencies fetched under $Destination"
Write-Host "Next: python $PSScriptRoot\generate-deps-projects.py --src $Destination"

$ca = Join-Path $root 'cacert.pem'
if (!(Test-Path $ca)) {
  Write-Host 'Fetching Mozilla CA bundle from curl.se...'
  Invoke-WebRequest -UseBasicParsing -Uri 'https://curl.se/ca/cacert.pem' -OutFile $ca
}
