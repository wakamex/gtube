#!/bin/sh
# Regenerates viz_bend.c and its GPU program viz.gpu from viz.bend. Needs Bend's compiler (with
# native Windows support: the windows-embed branch), bun, clang, and an NVIDIA GPU with CUDA's NVRTC.
# The GPU program is compiled for this machine's GPU and is keyed to the exact text of viz_bend.c,
# so both are committed together.
#   BEND=/code/bend-windows BUN=/code/bend2/tools/bun-linux-x64/bun gtube/bend/build.sh
set -e
cd "$(dirname "$0")"
"${BUN:-bun}" "${BEND:?set BEND to a Bend checkout}/bend2/main.ts" viz.bend -o viz_bend.c
tmp=$(mktemp -d)
cp viz_bend.c "$tmp/"
clang -std=c11 -O3 -w -DBEND_CUDA=1 "$tmp/viz_bend.c" -o "$tmp/viz" -lpthread -lm -ldl
"$tmp/viz" --gpu-build
cp "$tmp/viz.gpu" viz.gpu
rm -r "$tmp"
