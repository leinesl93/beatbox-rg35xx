#!/bin/sh
# Baut das Beatbox-Pak lokal mit Docker (Alternative zum GitHub-Workflow).
# Voraussetzung: Docker laeuft. Das Image ist ein arm64-Image; auf PCs mit Intel/AMD
# uebernimmt Docker Desktop die Emulation automatisch (dauert etwas laenger).
set -e
cd "$(dirname "$0")"
IMAGE=savant/minui-toolchain:rg35xx
docker run --rm --platform linux/arm64 -v "$PWD":/root/workspace/beatbox "$IMAGE" \
  /bin/bash -c "cd /root/workspace/beatbox && make pak"
echo
file dist/Beatbox.pak/beatbox 2>/dev/null || true
echo "Fertig: dist/Beatbox.pak -> auf die SD-Karte nach Tools/<Plattform>/ kopieren."
