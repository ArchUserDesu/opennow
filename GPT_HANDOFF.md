# OpenNOW-XEX Windows/XDK Handoff

## Current objective

Continue the Xbox 360 dashboard-native XEX/XDK port of OpenNOW until it builds a real `default.xex` on Windows.

Project path:

```text
C:\Users\0v3r10r6\Downloads\opennow-xex-complete-port\opennow-complete
```

This is now the active XDK/XEX build task. Preserve the old LibXenon target where practical, but do not switch back to XeLL/ELF as the primary target.

## SDK handling

**The user will provide the directory/path to their local Xbox 360 SDK/XDK installation.**

Do not download, search for, bundle, or guess an XDK path. If the SDK directory has not yet been supplied in the current Codex session, ask the user for that directory once.

After the user supplies it:

1. inspect that exact SDK directory;
2. locate the Xbox 360 compiler/VC toolchain, MSBuild or VCBuild integration, headers, import/static libraries, and XEX image tool;
3. configure the project/build scripts against those exact paths;
4. do not assume one fixed XDK release/layout.

Expected SDK components include equivalents of:

```text
Xbox 360 compiler / Visual C++ toolchain
MSBuild or VCBuild with Xbox 360 platform support
Xbox headers
xapilib.lib
xnet.lib
d3d9.lib
xaudio2.lib
XEX/image build tool (for example imagexex.exe depending on release)
```

## User requirements

- Produce a normal dashboard-launchable `default.xex`.
- H.264 decoding stays software-only.
- Direct3D is for UI/video presentation/scaling, not H.264 hardware decode.
- Use Xbox kernel networking so dashboard-configured Ethernet/Wi-Fi works.
- Controller via XInput.
- Audio via Xbox XAudio2/XAudio APIs supported by the supplied XDK.
- Preserve GFN/OpenNOW protocol behavior where possible.
- Fix bugs encountered during the port when safe.
- Do not claim hardware success until the user tests the XEX.

## Existing XDK port

Important files already present:

```text
xdk\OpenNOW-XEX.sln
xdk\OpenNOW-XEX.vcxproj
xdk\build.ps1
xdk\fetch-deps.ps1
xdk\generate-deps-projects.py
xdk\patch-deps.py
xdk\PROJECT_SETUP.md
xdk\compat\
```

The platform layer is intended to cover XNet/Winsock, dashboard networking/Wi-Fi, XInput, XAudio2, Direct3D9, local storage, TLS hooks, and an on-screen bitmap UI.

Expected runtime folder:

```text
default.xex
cacert.pem
```

## Dependency strategy

Do not reuse the old LibXenon/newlib static libraries for the XDK target.

Target dependencies:

```text
cJSON
Jansson
Opus
mbedTLS
libsrtp
libpeer
FFmpeg libavcodec + libavutil
```

Current design decisions:

- no curl WebSocket requirement on XEX;
- libpeer should use `CONFIG_USE_USRSCTP=0`, so usrsctp is not required;
- H.264 stays software-decoded with FFmpeg;
- use the Xbox 360/MSVC FFmpeg project files under the fetched XBMC-360 source as the decoder build base/reference.

Generated dependency projects should include:

```text
xdk\projects\cjson.vcxproj
xdk\projects\jansson.vcxproj
xdk\projects\opus.vcxproj
xdk\projects\mbedtls_opennow.vcxproj
xdk\projects\srtp2.vcxproj
xdk\projects\peer.vcxproj
```

If `xdk\deps-src` or these generated projects are absent in the Windows copy, fetch/regenerate them using the existing scripts.

## What Codex should do next

Work directly in:

```text
C:\Users\0v3r10r6\Downloads\opennow-xex-complete-port\opennow-complete
```

Then:

1. Read `AGENTS.md`, `GPT_HANDOFF.md`, `xdk\PROJECT_SETUP.md`, `xdk\build.ps1`, and the XDK project files.
2. Get the local SDK directory from the user if it has not already been provided.
3. Inspect that SDK directory and establish exact tool/include/lib paths.
4. Patch build scripts/projects to use the supplied SDK correctly on Windows.
5. Fetch/regenerate dependencies if needed.
6. Build the dependency projects for `Xbox 360`.
7. Fix compile/link errors iteratively.
8. Prefer fixes in `xdk\compat\`, dependency project definitions/patches, and `source\platform\xdk_*` before changing high-level GFN protocol logic.
9. Build software FFmpeg H.264 (`libavcodec` + `libavutil`).
10. Build the final XEX.
11. Produce:
   ```text
   xdk\bin\Release\default.xex
   xdk\bin\Release\cacert.pem
   ```
12. Inspect linker output for unresolved symbols/import problems.
13. Update this handoff with exact SDK path, tool versions, successful build commands, errors fixed, and remaining hardware-test items.
14. Tell the user the exact output path and how to copy/launch the XEX.

## Legacy compiler compatibility

Expect an old Xbox Visual C++ compiler. Only change syntax/library features when the actual supplied compiler rejects them. Watch for:

```text
nullptr
constexpr
range-for
initializer lists
std::to_string
raw string literals
newer STL APIs unavailable in the XDK CRT/STL
```

## Networking

The intended XEX networking sequence is equivalent to:

```text
XNetStartup
WSAStartup
XNetGetTitleXnAddr
Winsock TCP/UDP
```

Use the actual supplied-XDK APIs/signatures. This should use the console kernel's configured network interface, including Wi-Fi.

libpeer POSIX assumptions such as `getifaddrs`, `unistd.h`, `close`, and POSIX timing may need wrappers in `xdk\compat`.

## Validation standard

Do not call the build complete merely because `.vcxproj` files generate.

Build-stage completion means:

```text
default.xex exists
cacert.pem exists beside it
all dependency builds succeed
final link succeeds with no unresolved symbols
```

Runtime/network/streaming correctness still requires the user's console test.

## Repository note

This snapshot may not have `.git`. Do not assume version control exists. Make backups or generate patches before large edits.

---

============================================================
 REPOSITORY SNAPSHOT AT HANDOFF CREATION
============================================================

## pwd
/home/anon/Documents/Projects/Shared/OpenNOW-Xenon-complete-source/OpenNOW-Xenon

## git status --short
fatal: not a git repository (or any of the parent directories): .git

## git rev-parse HEAD
fatal: not a git repository (or any of the parent directories): .git

## git diff --stat
warning: Not a git repository. Use --no-index to compare two paths outside a working tree
usage: git diff --no-index [<options>] <path> <path> [<pathspec>...]

Diff output format options
    -p, --patch           generate patch
    -s, --no-patch        suppress diff output
    -u                    generate patch
    -U, --unified[=<n>]   generate diffs with <n> lines context
    -W, --[no-]function-context
                          generate diffs with <n> lines context
    --raw                 generate the diff in raw format
    --patch-with-raw      synonym for '-p --raw'
    --patch-with-stat     synonym for '-p --stat'
    --numstat             machine friendly --stat
    --shortstat           output only the last line of --stat
    -X, --dirstat[=<param1>,<param2>...]
                          output the distribution of relative amount of changes for each sub-directory
    --cumulative          synonym for --dirstat=cumulative
    --dirstat-by-file[=<param1>,<param2>...]
                          synonym for --dirstat=files,<param1>,<param2>...
    --check               warn if changes introduce conflict markers or whitespace errors
    --summary             condensed summary such as creations, renames and mode changes
    --name-only           show only names of changed files
    --name-status         show only names and status of changed files
    --stat[=<width>[,<name-width>[,<count>]]]
                          generate diffstat
    --stat-width <width>  generate diffstat with a given width
    --stat-name-width <width>
                          generate diffstat with a given name width
    --stat-graph-width <width>
                          generate diffstat with a given graph width
    --stat-count <count>  generate diffstat with limited lines
    --[no-]compact-summary
                          generate compact summary in diffstat
    --binary              output a binary diff that can be applied
    --[no-]full-index     show full pre- and post-image object names on the "index" lines
    --[no-]color[=<when>] show colored diff
    --ws-error-highlight <kind>
                          highlight whitespace errors in the 'context', 'old' or 'new' lines in the diff
    -z                    do not munge pathnames and use NULs as output field terminators in --raw or --numstat
    --[no-]abbrev[=<n>]   use <n> digits to display object names
    --src-prefix <prefix> show the given source prefix instead of "a/"
    --dst-prefix <prefix> show the given destination prefix instead of "b/"
    --line-prefix <prefix>
                          prepend an additional prefix to every line of output
    --no-prefix           do not show any source or destination prefix
    --default-prefix      use default prefixes a/ and b/
    --inter-hunk-context <n>
                          show context between diff hunks up to the specified number of lines
    --output-indicator-new <char>
                          specify the character to indicate a new line instead of '+'
    --output-indicator-old <char>
                          specify the character to indicate an old line instead of '-'
    --output-indicator-context <char>
                          specify the character to indicate a context instead of ' '

Diff rename options
    -B, --break-rewrites[=<n>[/<m>]]
                          break complete rewrite changes into pairs of delete and create
    -M, --find-renames[=<n>]
                          detect renames
    -D, --irreversible-delete
                          omit the preimage for deletes
    -C, --find-copies[=<n>]
                          detect copies
    --[no-]find-copies-harder
                          use unmodified files as source to find copies
    --no-renames          disable rename detection
    --[no-]rename-empty   use empty blobs as rename source
    --[no-]follow         continue listing the history of a file beyond renames
    -l <n>                prevent rename/copy detection if the number of rename/copy targets exceeds given limit

Diff algorithm options
    --minimal             produce the smallest possible diff
    -w, --ignore-all-space
                          ignore whitespace when comparing lines
    -b, --ignore-space-change
                          ignore changes in amount of whitespace
    --ignore-space-at-eol ignore changes in whitespace at EOL
    --ignore-cr-at-eol    ignore carrier-return at the end of line
    --ignore-blank-lines  ignore changes whose lines are all blank
    -I, --[no-]ignore-matching-lines <regex>
                          ignore changes whose all lines match <regex>
    --[no-]indent-heuristic
                          heuristic to shift diff hunk boundaries for easy reading
    --patience            generate diff using the "patience diff" algorithm
    --histogram           generate diff using the "histogram diff" algorithm
    --diff-algorithm <algorithm>
                          choose a diff algorithm
    --anchored <text>     generate diff using the "anchored diff" algorithm
    --word-diff[=<mode>]  show word diff, using <mode> to delimit changed words
    --word-diff-regex <regex>
                          use <regex> to decide what a word is
    --color-words[=<regex>]
                          equivalent to --word-diff=color --word-diff-regex=<regex>
    --[no-]color-moved[=<mode>]
                          moved lines of code are colored differently
    --[no-]color-moved-ws <mode>
                          how white spaces are ignored in --color-moved

Other diff options
    --[no-]relative[=<prefix>]
                          when run from subdir, exclude changes outside and show relative paths
    -a, --[no-]text       treat all files as text
    -R                    swap two inputs, reverse the diff
    --[no-]exit-code      exit with 1 if there were differences, 0 otherwise
    --[no-]quiet          disable all output of the program
    --[no-]ext-diff       allow an external diff helper to be executed
    --[no-]textconv       run external text conversion filters when comparing binary files
    --ignore-submodules[=<when>]
                          ignore changes to submodules in the diff generation
    --submodule[=<format>]
                          specify how differences in submodules are shown
    --ita-invisible-in-index
                          hide 'git add -N' entries from the index
    --ita-visible-in-index
                          treat 'git add -N' entries as real in the index
    -S <string>           look for differences that change the number of occurrences of the specified string
    -G <regex>            look for differences that change the number of occurrences of the specified regex
    --pickaxe-all         show all changes in the changeset with -S or -G
    --pickaxe-regex       treat <string> in -S as extended POSIX regular expression
    -O <file>             control the order in which files appear in the output
    --rotate-to <path>    show the change in the specified path first
    --skip-to <path>      skip the output to the specified path
    --find-object <object-id>
                          look for differences that change the number of occurrences of the specified object
    --diff-filter [(A|C|D|M|R|T|U|X|B)...[*]]
                          select files by diff type
    --max-depth <depth>   maximum tree depth to recurse
    --output <file>       output to a specific file


## Relevant Dockerfiles
./Dockerfile.xenon
./Dockerfile.xenon-builder

## Xenon compatibility files
third_party/xenon-compat/include/arpa/inet.h
third_party/xenon-compat/include/net/if.h
third_party/xenon-compat/include/netinet/in.h
third_party/xenon-compat/include/netinet/in.h.pre-ipport-reserved
third_party/xenon-compat/include/pthread.h
third_party/xenon-compat/include/pthread.h.pre-force-first
third_party/xenon-compat/include/sys/socket.h
third_party/xenon-compat/include/sys/socket.h.pre-cmsg
third_party/xenon-compat/include/sys/socket.h.pre-seqpacket

## Installed Xenon-prefix libraries currently in repository
libcjson.a
libjansson.a
libmbedcrypto.a
libmbedtls.a
libmbedx509.a
libopus.a
libsrtp2.a

## Installed Xenon-prefix headers, top level
cjson/cJSON.h
jansson_config.h
jansson.h
mbedtls/aes.h
mbedtls/aria.h
mbedtls/asn1.h
mbedtls/asn1write.h
mbedtls/base64.h
mbedtls/bignum.h
mbedtls/build_info.h
mbedtls/camellia.h
mbedtls/ccm.h
mbedtls/chacha20.h
mbedtls/chachapoly.h
mbedtls/check_config.h
mbedtls/cipher.h
mbedtls/cmac.h
mbedtls/compat-2.x.h
mbedtls/config_psa.h
mbedtls/constant_time.h
mbedtls/ctr_drbg.h
mbedtls/debug.h
mbedtls/des.h
mbedtls/dhm.h
mbedtls/ecdh.h
mbedtls/ecdsa.h
mbedtls/ecjpake.h
mbedtls/ecp.h
mbedtls/entropy.h
mbedtls/error.h
mbedtls/gcm.h
mbedtls/hkdf.h
mbedtls/hmac_drbg.h
mbedtls/legacy_or_psa.h
mbedtls/lms.h
mbedtls/mbedtls_config.h
mbedtls/md5.h
mbedtls/md.h
mbedtls/memory_buffer_alloc.h
mbedtls/net_sockets.h
mbedtls/nist_kw.h
mbedtls/oid.h
mbedtls/pem.h
mbedtls/pkcs12.h
mbedtls/pkcs5.h
mbedtls/pkcs7.h
mbedtls/pk.h
mbedtls/platform.h
mbedtls/platform_time.h
mbedtls/platform_util.h
mbedtls/poly1305.h
mbedtls/private_access.h
mbedtls/psa_util.h
mbedtls/ripemd160.h
mbedtls/rsa.h
mbedtls/sha1.h
mbedtls/sha256.h
mbedtls/sha512.h
mbedtls/ssl_cache.h
mbedtls/ssl_ciphersuites.h
mbedtls/ssl_cookie.h
mbedtls/ssl.h
mbedtls/ssl_ticket.h
mbedtls/threading.h
mbedtls/timing.h
mbedtls/version.h
mbedtls/x509_crl.h
mbedtls/x509_crt.h
mbedtls/x509_csr.h
mbedtls/x509.h
opus/opus_defines.h
opus/opus.h
opus/opus_multistream.h
opus/opus_projection.h
opus/opus_types.h
psa/crypto_builtin_composites.h
psa/crypto_builtin_primitives.h
psa/crypto_compat.h
psa/crypto_config.h
psa/crypto_driver_common.h
psa/crypto_driver_contexts_composites.h
psa/crypto_driver_contexts_primitives.h
psa/crypto_extra.h
psa/crypto.h
psa/crypto_platform.h
psa/crypto_se_driver.h
psa/crypto_sizes.h
psa/crypto_struct.h
psa/crypto_types.h
psa/crypto_values.h
srtp2/auth.h
srtp2/cipher.h
srtp2/crypto_types.h
srtp2/srtp.h
