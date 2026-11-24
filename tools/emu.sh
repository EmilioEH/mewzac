#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build only the browser emulator (build/web/), with the clang on this computer: no Docker, no JieLi
# toolchain. Needs clang and wasm-ld (lld). The FM-1 package needs the full build (./fm1 build, or CI).
#   tools/emu.sh          build/web/
#   tools/emu.sh serve    and serve it on http://localhost:8000
set -e
cd "$(dirname "$0")/.."
NAME=$(python3 -c 'import json;print(json.load(open("firmware.json"))["name"])')
VER=$(python3 -c 'import json;print(json.load(open("firmware.json"))["version"])')
ID=$(python3 -c 'import json;print(json.load(open("firmware.json"))["identity"])')
OPT=$(python3 -c '
import json
o = json.load(open("firmware.json")).get("options", {})
m = {"usb_audio": "FOMA_USB_AUDIO", "trs_midi": "FOMA_TRS_MIDI", "midi_clock": "FOMA_MIDI_CLOCK", "bluetooth": "FOMA_BLUETOOTH"}
print(" ".join("-D%s=%d" % (v, int(o.get(k, k != "bluetooth"))) for k, v in m.items()), "-DFOMA_MVAVE=0")')
mkdir -p build/web
cp web/* build/web/
# shellcheck disable=SC2086
clang --target=wasm32 -O2 -nostdlib -fno-builtin -Wall -Wno-unused-function -Wno-incompatible-library-redeclaration \
  -Iplatform -Iplatform/hal -Iplatform/src -Iplatform/gen -DAPP_NAME="\"$NAME\"" -DAPP_VER="\"$VER\"" $OPT \
  -Wl,--no-entry -Wl,-z,stack-size=1048576 -o build/web/firmware.wasm platform/web.c src/*.c
printf '{"id":"%s","wasm":true,"fwsc":false,"ok":true,"name":"%s","version":"%s","identity":"%s","log":"emulator only (tools/emu.sh)"}\n' \
  "$(date +%s)000000000" "$NAME" "$VER" "${ID}${VER##*.}" >build/web/build.json
echo "build/web/firmware.wasm $(($(wc -c <build/web/firmware.wasm) / 1024)) KiB"
if [ "$1" = serve ]; then
  echo "http://localhost:${PORT:-8000}"
  python3 -m http.server -d build/web "${PORT:-8000}"
fi
