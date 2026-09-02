#!/usr/bin/env sh
# Build a side-by-side debug tree containing this complete Xenon port and the
# exact OpenNOW-Switch revision it was based on. The upstream checkout is a
# reference only; no additional app wiring is required from it.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UPSTREAM="$ROOT/third_party/OpenNOW-Switch"
OUT="${1:-$ROOT/build-port-tree}"
if [ ! -d "$UPSTREAM/.git" ]; then "$ROOT/scripts/fetch-upstream.sh"; fi
rm -rf "$OUT"; mkdir -p "$OUT/upstream" "$OUT/xenon"
( cd "$UPSTREAM"; git archive HEAD | tar -x -C "$OUT/upstream" )
tar -C "$ROOT" --exclude='./build' --exclude='./build-xenon' --exclude='./build-port-tree' \
  --exclude='./third_party/OpenNOW-Switch' --exclude='./third_party/xenon-prefix' -cf - \
  ./AGENTS.md ./CMakeLists.txt ./LICENSE ./Makefile ./Makefile.xenon ./README.md \
  ./docs ./include ./scripts ./source ./tests ./third_party/README.md | tar -x -C "$OUT/xenon"
cat > "$OUT/README-ASSEMBLED.txt" <<'TXT'
upstream/ is the pinned OpenNOW-Switch reference.
xenon/ is the complete native Xbox 360 port source tree.
The upstream directory is supplied for diffing/debugging, not to provide a missing integration layer.
TXT
echo "Assembled debug/reference tree at: $OUT"
