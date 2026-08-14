#!/bin/sh
PAKDIR="$(dirname "$0")"
cd "$PAKDIR"

export LD_LIBRARY_PATH="/usr/trimui/lib:$LD_LIBRARY_PATH"
unset SDL_VIDEODRIVER   # dejar que SDL2 auto-detecte (NO poner offscreen)
export HOME=/mnt/SDCARD
export SDL_AUDIODRIVER=alsa

chmod +x "$PAKDIR/ftpgui"
exec ./ftpgui
