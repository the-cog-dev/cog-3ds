#!/usr/bin/env bash
# Build cog-3ds.cia — a HOME-menu-installable version of The Cog.
#
# Prereqs (already in repo):
#   - tools/bannertool.exe, tools/makerom.exe
#   - meta/banner.png (256x128), meta/silence.wav
#   - meta/icon.png (48x48), meta/cog.rsf
#   - build/cog-3ds.elf (produced by `bash build.sh` first)
#
# Output: cog-3ds.cia in the repo root. Install via FBI.

set -e
cd "$(dirname "$0")"

if [ ! -f cog-3ds.elf ]; then
  echo "❌ cog-3ds.elf not found — run 'bash build.sh' first"
  exit 1
fi

mkdir -p build

echo "▶ banner.bnr"
tools/bannertool.exe makebanner \
  -i meta/banner.png \
  -a meta/silence.wav \
  -o build/banner.bnr

echo "▶ icon.icn"
tools/bannertool.exe makesmdh \
  -s "The Cog" \
  -l "Agent orchestration from your 3DS" \
  -p "theCog.dev" \
  -i meta/icon.png \
  -o build/icon.icn

echo "▶ cog-3ds.cia"
tools/makerom.exe -f cia -target t -exefslogo \
  -rsf meta/cog.rsf \
  -elf cog-3ds.elf \
  -icon build/icon.icn \
  -banner build/banner.bnr \
  -o cog-3ds.cia

echo "✅ cog-3ds.cia built ($(du -h cog-3ds.cia | cut -f1))"
echo ""
echo "To install on 3DS:"
echo "  1. Copy cog-3ds.cia to SD card (e.g. H:/cog-3ds.cia)"
echo "  2. Boot FBI → SD → select cog-3ds.cia → Install and delete CIA"
echo "  3. Exit to HOME menu — the golden gear is now an app"
