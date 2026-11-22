#!/bin/sh
# Beatbox -- MinUI-Tool-Pak. Startet das Programm aus dem eigenen Ordner.
cd "$(dirname "$0")" || exit 1
chmod +x ./beatbox 2>/dev/null
./beatbox "$(pwd)" > beatbox.log 2>&1
sync
