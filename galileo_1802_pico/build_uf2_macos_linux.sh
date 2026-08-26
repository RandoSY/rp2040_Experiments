#!/usr/bin/env bash
set -euo pipefail

echo "Galileo 1802 Pico UF2 build"
echo "==========================="

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "ERROR: arduino-cli is not in PATH." >&2
  exit 1
fi

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_DIR"
INDEX_URL="https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json"

echo
echo "[1/4] Arduino CLI version"
arduino-cli version

echo
echo "[2/4] Refresh Arduino-Pico package index"
arduino-cli core update-index --additional-urls "$INDEX_URL"

echo
echo "[3/4] Install pinned Arduino-Pico core 6.0.0"
arduino-cli core install "rp2040:rp2040@6.0.0" --additional-urls "$INDEX_URL"

echo
echo "[4/4] Compile Raspberry Pi Pico UF2"
BUILD_DIR="$PROJECT_DIR/build/galileo_1802_pico"
mkdir -p "$BUILD_DIR"

arduino-cli compile \
  --fqbn "rp2040:rp2040:rpipico:flash=2097152_65536,usbstack=picosdk,opt=Optimize2" \
  --warnings all \
  --output-dir "$BUILD_DIR" \
  "$PROJECT_DIR"

echo
echo "Build complete. UF2 file(s):"
find "$BUILD_DIR" -maxdepth 1 -name '*.uf2' -print
