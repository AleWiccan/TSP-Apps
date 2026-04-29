#!/bin/sh
# launch.sh — Trimui Smart Pro (Stock OS / MinUI / CrossMix)
#
# El OS ejecuta este script desde la carpeta .pak.
# Patrón confirmado por la comunidad (joyrider3774, ryanmsartor, etc.)

PAKDIR="$(dirname "$0")"
cd "$PAKDIR"

# Librerías del sistema Trimui (SDL2, SDL2_ttf, etc.)
export LD_LIBRARY_PATH="/usr/trimui/lib:$LD_LIBRARY_PATH"

# Sin SDL_VIDEODRIVER: SDL2 auto-detecta el driver correcto (kmsdrm/fbdev)
# No forzar offscreen ni ningún otro driver falso
#unset SDL_VIDEODRIVER

export HOME=/mnt/SDCARD
export SDL_AUDIODRIVER=alsa

chmod +x "$PAKDIR/Calculator"
./Calculator
