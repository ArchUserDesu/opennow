param(
  [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
  [string]$XdkRoot = 'C:\Program Files (x86)\Microsoft Xbox 360 SDK'
)

$ErrorActionPreference = 'Stop'
$here = (Resolve-Path $PSScriptRoot).Path
$root = (Resolve-Path (Join-Path $here '..')).Path
$cl = Join-Path $XdkRoot 'bin\win32\cl.exe'
$libTool = Join-Path $XdkRoot 'bin\win32\lib.exe'
$imagexex = Join-Path $XdkRoot 'bin\win32\imagexex.exe'
foreach ($tool in @($cl, $libTool, $imagexex)) {
  if (!(Test-Path $tool)) { throw "Missing XDK tool: $tool" }
}

$env:XEDK = $XdkRoot
$env:Path = (Join-Path $XdkRoot 'bin\win32') + ';' + $env:Path
# The Xbox copy of shared headers (notably xnamath.h) must precede win32.
$env:INCLUDE = (Join-Path $XdkRoot 'include\xbox') + ';' +
               (Join-Path $XdkRoot 'include\xbox\sys') + ';' +
               (Join-Path $XdkRoot 'include\win32')
$env:LIB = (Join-Path $XdkRoot 'lib\xbox') + ';' +
           (Join-Path $XdkRoot 'lib\win32')

function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
  & $Exe @Arguments
  if ($LASTEXITCODE -ne 0) { throw "Command failed ($LASTEXITCODE): $Exe" }
}

function Expand-ProjectList([string]$Value, [string]$ProjectDir) {
  $result = @()
  if (!$Value) { return $result }
  foreach ($entry in ($Value -split ';')) {
    $entry = $entry.Trim()
    if (!$entry -or $entry -match '^%\(') { continue }
    $entry = $entry.Replace('$(ProjectDir)', $ProjectDir + '\')
    if ($entry -match '^\$\(') { continue }
    if (![IO.Path]::IsPathRooted($entry)) { $entry = Join-Path $ProjectDir $entry }
    $result += [IO.Path]::GetFullPath($entry)
  }
  return $result
}

function Get-ReleaseDefinition([xml]$Xml, [Xml.XmlNamespaceManager]$Ns) {
  $nodes = $Xml.SelectNodes('//m:ItemDefinitionGroup', $Ns)
  foreach ($node in $nodes) {
    if ($node.Condition -and $node.Condition -like "*'$Configuration|Xbox 360'*") { return $node }
  }
  throw "No $Configuration|Xbox 360 settings found"
}

function Build-StaticProject([string]$ProjectPath, [string]$OutputName) {
  $ProjectPath = [IO.Path]::GetFullPath($ProjectPath)
  $projectDir = Split-Path $ProjectPath -Parent
  [xml]$xml = Get-Content -Raw $ProjectPath
  $ns = New-Object Xml.XmlNamespaceManager($xml.NameTable)
  $ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
  $definition = Get-ReleaseDefinition $xml $ns
  $compile = $definition.ClCompile
  $includes = Expand-ProjectList ([string]$compile.AdditionalIncludeDirectories) $projectDir
  $defines = @()
  foreach ($define in (([string]$compile.PreprocessorDefinitions) -split ';')) {
    $define = $define.Trim()
    if ($define -and $define -notmatch '^%\(') { $defines += $define }
  }
  $forced = @()
  foreach ($header in (([string]$compile.ForcedIncludeFiles) -split ';')) {
    $header = $header.Trim()
    if ($header -and $header -notmatch '^%\(') {
      if (![IO.Path]::IsPathRooted($header)) {
        $candidate = Join-Path $projectDir $header
        if (Test-Path $candidate) { $header = [IO.Path]::GetFullPath($candidate) }
      }
      $forced += $header
    }
  }

  $objDir = Join-Path $here "obj\direct\$OutputName\$Configuration"
  $outDir = Join-Path $here "lib\$Configuration"
  New-Item -ItemType Directory -Force $objDir, $outDir | Out-Null
  $sources = @()
  foreach ($node in $xml.SelectNodes('//m:ClCompile[@Include]', $ns)) {
    $excluded = $false
    foreach ($ex in $node.ExcludedFromBuild) {
      if ($ex.Condition -like "*'$Configuration|Xbox 360'*" -and ([string]$ex).Trim().ToLower() -eq 'true') { $excluded = $true }
    }
    if (!$excluded) {
      $source = [string]$node.Include
      if (![IO.Path]::IsPathRooted($source)) { $source = Join-Path $projectDir $source }
      $sources += [IO.Path]::GetFullPath($source)
    }
  }
  Write-Host "==> $OutputName ($($sources.Count) sources)"
  $objects = @()
  $common = @('/nologo','/c','/W3','/GS-')
  if ($Configuration -eq 'Release') { $common += @('/O2','/Oi','/Ot','/MT') } else { $common += @('/Od','/MTd','/Zi') }
  # mbedTLS and libpeer contain modern declarations that the XDK's C89 frontend
  # cannot parse.  Compile those archives through the same XDK C++ frontend;
  # libpeer gets C allocation bridging from opennow_xdk_compat.h.
  if ($OutputName -eq 'mbedtls_opennow' -or $OutputName -eq 'peer') { $common += '/TP' }
  elseif (([string]$compile.CompileAs) -eq 'CompileAsC') { $common += '/TC' }
  foreach ($inc in $includes) { $common += "/I$inc" }
  foreach ($def in $defines) { $common += ('/D' + $def.Replace('"','\"')) }
  foreach ($fi in $forced) { $common += "/FI$fi" }

  # This direct builder has no compiler-generated dependency database.  Use
  # the newest project/include header as a conservative project dependency so
  # a struct or inline change can never leave ABI-incompatible stale objects
  # in the same archive.
  $dependencyTime = (Get-Item $ProjectPath).LastWriteTimeUtc
  $headerRoots = @($includes + ($sources | ForEach-Object { Split-Path $_ -Parent })) | Select-Object -Unique
  foreach ($headerRoot in $headerRoots) {
    if (!(Test-Path -LiteralPath $headerRoot -PathType Container)) { continue }
    foreach ($headerFile in (Get-ChildItem -LiteralPath $headerRoot -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.h','.hpp','.hh','.inl' })) {
      if ($headerFile.LastWriteTimeUtc -gt $dependencyTime) { $dependencyTime = $headerFile.LastWriteTimeUtc }
    }
  }

  Push-Location $projectDir
  try {
    for ($i = 0; $i -lt $sources.Count; $i++) {
      $obj = Join-Path $objDir (('{0:D4}_' -f $i) + [IO.Path]::GetFileNameWithoutExtension($sources[$i]) + '.obj')
      $inputTime = (Get-Item $sources[$i]).LastWriteTimeUtc
      $projectTime = (Get-Item $ProjectPath).LastWriteTimeUtc
      if ($projectTime -gt $inputTime) { $inputTime = $projectTime }
      if ($dependencyTime -gt $inputTime) { $inputTime = $dependencyTime }
      foreach ($forcedHeader in $forced) {
        if (Test-Path $forcedHeader) {
          $forcedTime = (Get-Item $forcedHeader).LastWriteTimeUtc
          if ($forcedTime -gt $inputTime) { $inputTime = $forcedTime }
        }
      }
      if ($OutputName -eq 'mbedtls_opennow') {
        $configTime = (Get-Item (Join-Path $here 'compat\mbedtls_xdk_config.h')).LastWriteTimeUtc
        if ($configTime -gt $inputTime) { $inputTime = $configTime }
      }
      if ((Test-Path $obj) -and (Get-Item $obj).LastWriteTimeUtc -ge $inputTime) {
        $objects += $obj
        continue
      }
      Invoke-Checked $cl ($common + @("/Fo$obj", $sources[$i]))
      $objects += $obj
    }
  } finally { Pop-Location }
  $library = Join-Path $outDir ($OutputName + '.lib')
  $rsp = Join-Path $objDir 'objects.rsp'
  $objects | ForEach-Object { '"' + $_ + '"' } | Set-Content -Encoding ASCII $rsp
  Invoke-Checked $libTool @('/nologo', "/OUT:$library", "@$rsp")
}

$projects = Join-Path $here 'projects'
Build-StaticProject (Join-Path $projects 'cjson.vcxproj') 'cjson'
Build-StaticProject (Join-Path $projects 'jansson.vcxproj') 'jansson'
Build-StaticProject (Join-Path $projects 'opus.vcxproj') 'opus'
Build-StaticProject (Join-Path $projects 'mbedtls_opennow.vcxproj') 'mbedtls_opennow'
Build-StaticProject (Join-Path $projects 'srtp2.vcxproj') 'srtp2'
Build-StaticProject (Join-Path $projects 'peer.vcxproj') 'peer'

$ff = Join-Path $here 'deps-src\xbmc360\libraries\ffmpeg\vcproj'
Build-StaticProject (Join-Path $ff 'pthreads\pthreads.vcxproj') 'pthreads'
Build-StaticProject (Join-Path $ff 'libavutil\libavutil.vcxproj') 'libavutil'
Build-StaticProject (Join-Path $ff 'libavcodec\libavcodec.vcxproj') 'libavcodec'

[xml]$mainProject = Get-Content -Raw (Join-Path $here 'OpenNOW-XEX.vcxproj')
$ns = New-Object Xml.XmlNamespaceManager($mainProject.NameTable)
$ns.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
$definition = Get-ReleaseDefinition $mainProject $ns
$compile = $definition.ClCompile
$includes = Expand-ProjectList ([string]$compile.AdditionalIncludeDirectories) $here
$defines = (([string]$compile.PreprocessorDefinitions) -split ';') | Where-Object { $_ -and $_ -notmatch '^%\(' }
$objDir = Join-Path $here "obj\direct\opennow\$Configuration"
$outDir = Join-Path $here "bin\$Configuration"
New-Item -ItemType Directory -Force $objDir, $outDir | Out-Null
$objects = @()
$sources = $mainProject.SelectNodes('//m:ClCompile[@Include]', $ns)
Write-Host "==> OpenNOW-XEX ($($sources.Count) sources)"
$mainDependencyTime = (Get-Item (Join-Path $here 'OpenNOW-XEX.vcxproj')).LastWriteTimeUtc
$mainSourceDirs = @()
foreach ($sourceNode in @($sources)) {
  $mainSourcePath = [IO.Path]::GetFullPath((Join-Path $here ([string]$sourceNode.Include)))
  $mainSourceDirs += Split-Path -Path $mainSourcePath -Parent
}
$mainHeaderRoots = @($includes + $mainSourceDirs) | Select-Object -Unique
foreach ($headerRoot in $mainHeaderRoots) {
  if (!(Test-Path -LiteralPath $headerRoot -PathType Container)) { continue }
  foreach ($headerFile in (Get-ChildItem -LiteralPath $headerRoot -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in '.h','.hpp','.hh','.inl' })) {
    if ($headerFile.LastWriteTimeUtc -gt $mainDependencyTime) { $mainDependencyTime = $headerFile.LastWriteTimeUtc }
  }
}
for ($i = 0; $i -lt $sources.Count; $i++) {
  $source = [IO.Path]::GetFullPath((Join-Path $here ([string]$sources[$i].Include)))
  $obj = Join-Path $objDir (('{0:D3}_' -f $i) + [IO.Path]::GetFileNameWithoutExtension($source) + '.obj')
  $mainInputTime = (Get-Item $source).LastWriteTimeUtc
  if ($mainDependencyTime -gt $mainInputTime) { $mainInputTime = $mainDependencyTime }
  if ((Test-Path $obj) -and (Get-Item $obj).LastWriteTimeUtc -ge $mainInputTime) {
    $objects += $obj
    continue
  }
  $args = @('/nologo','/c','/W3','/EHsc','/GS-')
  if ($Configuration -eq 'Release') { $args += @('/O2','/Oi','/Ot','/MT') } else { $args += @('/Od','/MTd','/Zi') }
  foreach ($inc in $includes) { $args += "/I$inc" }
  foreach ($def in $defines) { $args += ('/D' + $def.Replace('"','\"')) }
  Invoke-Checked $cl ($args + @("/Fo$obj", $source))
  $objects += $obj
}

$pe = Join-Path $outDir 'default.pe'
$xdb = Join-Path $outDir 'default.xdb'
$libs = @('peer.lib','srtp2.lib','mbedtls_opennow.lib','cjson.lib','jansson.lib','opus.lib','libavcodec.lib','libavutil.lib','pthreads.lib','xnet.lib','xauth.lib','xapilib.lib','d3d9.lib','d3dx9.lib','xgraphics.lib','xaudio2.lib','xmcore.lib','xbdm.lib','xboxkrnl.lib')
$linkRsp = Join-Path $objDir 'link.rsp'
$linkLines = @('/nologo', '/XEX:NO', "/OUT:$pe", "/PDB:$xdb")
$linkLines += $objects | ForEach-Object { '"' + $_ + '"' }
$linkLines += "/LIBPATH:$(Join-Path $here "lib\$Configuration")"
$linkLines += $libs
$linkLines | Set-Content -Encoding ASCII $linkRsp
Invoke-Checked (Join-Path $XdkRoot 'bin\win32\link.exe') @("@$linkRsp")

$xex = Join-Path $outDir 'default.xex'
$xexConfig = Join-Path $here 'xex.xml'
if (!(Test-Path $xexConfig)) { throw 'xdk\xex.xml is missing' }
Invoke-Checked $imagexex @("/IN:$pe", "/OUT:$xex", "/CONFIG:$xexConfig")
$ca = Join-Path $here 'cacert.pem'
if (!(Test-Path $ca)) { throw 'xdk\cacert.pem is missing' }
Copy-Item -Force $ca (Join-Path $outDir 'cacert.pem')
Write-Host "Build output: $xex"
