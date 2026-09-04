#!/usr/bin/env python3
"""Generate small VS/Xbox 360 static-library projects around fetched C dependencies.
The XDK supplies the Xbox 360 platform target; this script only emits project inputs.
"""
from __future__ import print_function
import argparse, os, uuid
from pathlib import Path

HEADER = r'''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
 <ItemGroup Label="ProjectConfigurations">
  <ProjectConfiguration Include="Debug|Xbox 360"><Configuration>Debug</Configuration><Platform>Xbox 360</Platform></ProjectConfiguration>
  <ProjectConfiguration Include="Release|Xbox 360"><Configuration>Release</Configuration><Platform>Xbox 360</Platform></ProjectConfiguration>
 </ItemGroup>
 <PropertyGroup Label="Globals"><ProjectGuid>{%s}</ProjectGuid><RootNamespace>%s</RootNamespace></PropertyGroup>
 <PropertyGroup><OutDir>$(ProjectDir)..\lib\$(Configuration)\</OutDir><IntDir>$(ProjectDir)..\obj\%s\$(Configuration)\</IntDir></PropertyGroup>
 <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
 <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|Xbox 360'" Label="Configuration"><ConfigurationType>StaticLibrary</ConfigurationType><CharacterSet>NotSet</CharacterSet></PropertyGroup>
 <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|Xbox 360'" Label="Configuration"><ConfigurationType>StaticLibrary</ConfigurationType><CharacterSet>NotSet</CharacterSet><WholeProgramOptimization>false</WholeProgramOptimization></PropertyGroup>
 <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
'''
TAIL = ''' <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />\n</Project>\n'''

def xml(s): return str(s).replace('&','&amp;').replace('<','&lt;').replace('>','&gt;')
def rel(p, base): return os.path.relpath(str(p), str(base)).replace('/','\\')

def emit(name, out, sources, includes, defines, forced=None):
    out.parent.mkdir(parents=True, exist_ok=True)
    guid = str(uuid.uuid5(uuid.NAMESPACE_URL, 'opennow-xdk-'+name)).upper()
    common_inc = ';'.join(xml(rel(p,out.parent)) for p in includes) + ';$(ProjectDir)..\\compat;%(AdditionalIncludeDirectories)'
    common_def = ';'.join(defines) + ';_XBOX;OPENNOW_XDK=1;%(PreprocessorDefinitions)'
    with out.open('w', newline='\n') as f:
        f.write(HEADER % (guid,name,name))
        for cfg in ('Debug','Release'):
            f.write(' <ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|Xbox 360\'">\n' % cfg)
            f.write('  <ClCompile><AdditionalIncludeDirectories>%s</AdditionalIncludeDirectories><PreprocessorDefinitions>%s</PreprocessorDefinitions><CompileAs>CompileAsC</CompileAs><WarningLevel>Level3</WarningLevel>' % (common_inc, common_def))
            if forced: f.write('<ForcedIncludeFiles>%s;%%(ForcedIncludeFiles)</ForcedIncludeFiles>' % xml(rel(forced,out.parent)))
            f.write('</ClCompile>\n </ItemDefinitionGroup>\n')
        f.write(' <ItemGroup>\n')
        for p in sources: f.write('  <ClCompile Include="%s" />\n' % xml(rel(p,out.parent)))
        f.write(' </ItemGroup>\n'+TAIL)
    return guid

def files(root, patterns, excludes=()):
    out=[]
    for pat in patterns: out += list(root.glob(pat))
    ex=set(str((root/e).resolve()).lower() for e in excludes)
    return sorted([p for p in out if p.is_file() and str(p.resolve()).lower() not in ex])

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--src', required=True); a=ap.parse_args()
    src=Path(a.src).resolve(); here=Path(__file__).resolve().parent; proj=here/'projects'; compat=here/'compat'/'opennow_xdk_compat.h'
    sw=src/'opennow-switch'/'extern'/'libpeer'; peer=sw/'src'
    if not peer.exists(): raise SystemExit('libpeer missing; run fetch-deps.ps1 first')

    cjson=sw/'third_party'/'cJSON'
    mbed=sw/'third_party'/'mbedtls'
    srtp=sw/'third_party'/'libsrtp'
    jansson=src/'jansson'; opus=src/'opus'

    emit('cjson',proj/'cjson.vcxproj',[cjson/'cJSON.c'],[cjson],[],compat)
    emit('jansson',proj/'jansson.vcxproj',files(jansson/'src',['*.c'],['jansson_config.c']),[jansson/'src',jansson],[],compat)
    emit('opus',proj/'opus.vcxproj',files(opus,['celt/*.c','silk/*.c','silk/float/*.c','src/*.c'],['src/opus_demo.c','src/opus_compare.c','src/repacketizer_demo.c']),[opus/'include',opus,opus/'celt',opus/'silk',opus/'silk'/'float'],['OPUS_BUILD','USE_ALLOCA'],compat)

    # mbedTLS: compile crypto+x509+TLS into one archive. OpenNOW does not use
    # mbedTLS's POSIX networking module; the XDK TLS transport supplies BIOs.
    mbed_sources=files(mbed/'library',['*.c'],['net_sockets.c'])
    emit('mbedtls_opennow',proj/'mbedtls_opennow.vcxproj',mbed_sources,[mbed/'include',mbed/'library'],['MBEDTLS_CONFIG_FILE=\"mbedtls_xdk_config.h\"'],compat)

    # libsrtp core only, no test/sample programs.  Do not compile optional
    # OpenSSL/NSS/mbedTLS crypto backends: OpenNOW uses libsrtp's built-in
    # AES/AES-ICM/SHA1/HMAC implementation on Xbox.  Including those backend
    # translation units makes the old XDK compiler chase APIs/configuration
    # that are deliberately disabled.
    srtp_sources=files(srtp,['srtp/*.c','crypto/cipher/*.c','crypto/hash/*.c','crypto/kernel/*.c','crypto/math/*.c','crypto/replay/*.c'])
    srtp_sources=[p for p in srtp_sources if not p.name.lower().endswith(('_mbedtls.c','_nss.c','_ossl.c'))]
    emit('srtp2',proj/'srtp2.vcxproj',srtp_sources,[here/'compat'/'srtp',srtp/'include',srtp/'crypto'/'include',srtp],['HAVE_CONFIG_H=1'],compat)

    # OpenNOW uses libpeer only as the WebRTC engine. Disable its optional HTTP/
    # MQTT signaling and select its built-in SCTP implementation, removing the
    # usrsctp dependency and a large OS-porting surface.  libpeer itself uses
    # C99-style mixed declarations throughout, so the direct XDK build compiles
    # it with the C++ frontend while preserving C allocation semantics through
    # opennow_xdk_compat.h.
    peer_sources=files(peer,['*.c'],['peer_signaling.c'])
    emit('peer',proj/'peer.vcxproj',peer_sources,[peer,sw/'include',cjson,mbed/'include',srtp/'include',srtp/'crypto'/'include'],['CONFIG_USE_USRSCTP=0','DISABLE_PEER_SIGNALING=1','CONFIG_IPV6=0','LOG_REDIRECT=1','OPENNOW_PEER_CPP=1','MBEDTLS_CONFIG_FILE=\"mbedtls_xdk_config.h\"'],compat)
    print('Generated projects in', proj)
    print('FFmpeg projects are already provided by deps-src/xbmc360/libraries/ffmpeg/vcproj.')

if __name__=='__main__': main()
