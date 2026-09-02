#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ENGINE=${CONTAINER_ENGINE:-docker}
exec "$ENGINE" run --rm -it -v "$ROOT:/src" -w /src free60/libxenon:latest make xenon
