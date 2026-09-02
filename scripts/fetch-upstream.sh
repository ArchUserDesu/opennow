#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DST="$ROOT/third_party/OpenNOW-Switch"
REV=dce9743f183a5c211bd5971d02993e8b7253cf4d
if [ -d "$DST/.git" ]; then git -C "$DST" fetch --depth 1 origin "$REV"; git -C "$DST" checkout --detach "$REV"; else git clone https://github.com/OpenCloudGaming/OpenNOW-Switch.git "$DST"; git -C "$DST" checkout --detach "$REV"; fi
printf 'Staged OpenNOW-Switch at %s\n' "$REV"
