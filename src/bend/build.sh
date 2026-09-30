#!/bin/sh
# Regenerates viz_bend.c and its GPU program viz.gpu from viz.bend. Needs Bend's compiler (the
# vulkan-spike branch, with the Vulkan backend), bun, clang, Slang's slangc, and a Vulkan GPU.
# The GPU program is SPIR-V, keyed to the exact text of viz_bend.c, so both are committed together.
#   BEND=/path/to/bend BUN=/path/to/bun BEND_SLANGC=/path/to/slangc src/bend/build.sh
set -e
cd "$(dirname "$0")"
"${BUN:-bun}" "${BEND:?set BEND to a Bend checkout}/bend2/main.ts" viz.bend -o viz_bend.c
tmp=$(mktemp -d)
cp viz_bend.c "$tmp/"
clang -std=c11 -O3 -w -DBEND_VULKAN=1 "$tmp/viz_bend.c" -o "$tmp/viz" -lpthread -lm -ldl
"$tmp/viz" --gpu-build
cp "$tmp/viz.gpu" viz.gpu
rm -r "$tmp"
