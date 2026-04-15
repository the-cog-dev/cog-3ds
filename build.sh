#!/usr/bin/env bash
# Build wrapper. Uses devkitPro's msys2 bash as a LOGIN shell so its
# /etc/profile.d sets DEVKITPRO/DEVKITARM correctly.
#
# Usage:
#   bash build.sh              build only
#   bash build.sh clean        clean build artifacts
#   bash build.sh deploy       build + push over WiFi via 3dslink
#   bash build.sh deploy <ip>  build + push to a specific 3DS IP
#
# To use deploy: on your 3DS, open Homebrew Launcher → press Y →
# "Receive 3dsx via network". Then run this script with `deploy`.

set -e

DEVKITPRO_BASH="/f/coding/Decompiles/Tools/devkitPro/msys2/usr/bin/bash.exe"

if [ ! -x "$DEVKITPRO_BASH" ]; then
    echo "ERROR: devkitPro msys2 bash not found at $DEVKITPRO_BASH"
    exit 1
fi

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ "$1" = "deploy" ]; then
    shift
    DEPLOY_IP="${1:-}"
    "$DEVKITPRO_BASH" -lc "cd '$PROJECT_DIR' && make && 3dslink ${DEPLOY_IP:+--address=$DEPLOY_IP} cog-3ds.3dsx"
elif [ "$1" = "clean" ]; then
    "$DEVKITPRO_BASH" -lc "cd '$PROJECT_DIR' && make clean"
else
    "$DEVKITPRO_BASH" -lc "cd '$PROJECT_DIR' && make $*"
fi
