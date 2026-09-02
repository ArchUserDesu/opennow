#!/usr/bin/env sh
set -eu
: "${DEVKITXENON:?Set DEVKITXENON, usually /usr/local/xenon}"
for p in "$DEVKITXENON/bin/xenon-gcc" "$DEVKITXENON/bin/xenon-g++" "$DEVKITXENON/rules"; do [ -e "$p" ] || { echo "missing $p" >&2; exit 1; }; done
echo "LibXenon toolchain looks present: $DEVKITXENON"
