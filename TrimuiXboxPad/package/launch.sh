#!/bin/sh
# launch.sh - escribe TODO en log.txt (junto a la app) para poder depurar
PAKDIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PAKDIR" || exit 1
# Logs DESACTIVADOS por defecto. Para depurar, crea un archivo vacio
# "debug.on" junto a la app: entonces se escribe log.txt (y agent.log,
# bluetoothd.log, autoyes.log) en esta carpeta.
if [ -f "$PAKDIR/debug.on" ]; then
  DEBUG=1
  exec >"$PAKDIR/log.txt" 2>&1
else
  DEBUG=0
  exec >/dev/null 2>&1
  rm -f "$PAKDIR/log.txt" "$PAKDIR/agent.log" "$PAKDIR/autoyes.log" "$PAKDIR/bluetoothd.log"
fi

echo "[launcher] version 4 - $(date)"
echo "[launcher] uid=$(id -u) pwd=$PWD"

export LD_LIBRARY_PATH="/usr/trimui/lib:$LD_LIBRARY_PATH"
unset SDL_VIDEODRIVER
export HOME=/mnt/SDCARD
export SDL_AUDIODRIVER=alsa
chmod +x "$PAKDIR/TrimuiXboxPad" "$PAKDIR/bt-setup.sh" "$PAKDIR/bt-teardown.sh" "$PAKDIR/bt-autoyes.sh" 2>/dev/null

if [ "$DEBUG" = 1 ]; then
  echo "[launcher] --- estado BT antes ---"
  ps 2>/dev/null | grep -i -E "bluetoothd|bluez|hciattach|brcm|rtk|aic" | grep -v grep
  for t in bluetoothctl hciconfig btmgmt sdptool rfkill timeout; do
    command -v $t >/dev/null 2>&1 && echo "tiene: $t" || echo "NO tiene: $t"
  done
fi

# Para aislar el fallo: crea un archivo vacio "skip-setup" junto a la app
# y se salta bt-setup.sh (la app arranca igual).
if [ -f "$PAKDIR/skip-setup" ]; then
  echo "[launcher] skip-setup presente: NO ejecuto bt-setup.sh"
else
  echo "[launcher] ejecutando bt-setup.sh"
  sh "$PAKDIR/bt-setup.sh"
  echo "[launcher] bt-setup.sh termino (codigo $?)"
fi

echo "[launcher] lanzando binario"
./TrimuiXboxPad
CODE=$?
echo "[launcher] binario termino con codigo $CODE (139=segfault, 127=falta libreria)"
if [ ! -f "$PAKDIR/skip-setup" ]; then
  sh "$PAKDIR/bt-teardown.sh"
fi
if [ "$DEBUG" = 1 ]; then
  echo "[launcher] --- estado BT despues ---"
  ps 2>/dev/null | grep -i -E "bluetoothd|bluez" | grep -v grep
  dmesg 2>/dev/null | tail -15
fi
