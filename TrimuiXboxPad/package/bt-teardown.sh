#!/bin/sh
# bt-teardown.sh - cierra el agente y restaura bluetoothd tal como estaba.
STATE=/tmp/xboxpad_bt.state
FIFO=/tmp/xboxpad_btctl.fifo
T=""; command -v timeout >/dev/null 2>&1 && T="timeout 10"

echo "[bt-teardown] inicio"
for f in /tmp/xboxpad_btctl.pid /tmp/xboxpad_sleep.pid /tmp/xboxpad_watch.pid; do
  [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null
  rm -f "$f"
done
rm -f "$FIFO"

if [ -f "$STATE" ]; then
  ORIG=$(cat "$STATE")
  PID=$(ps 2>/dev/null | grep '[b]luetoothd' | head -n1 | sed 's/^ *//' | cut -d' ' -f1)
  [ -n "$PID" ] && { kill "$PID"; sleep 1; kill -0 "$PID" 2>/dev/null && kill -9 "$PID"; sleep 1; }
  echo "[bt-teardown] restaurando: $ORIG"
  $ORIG > /dev/null 2>&1 &
  sleep 2
  $T bluetoothctl power on 2>&1 | tail -2
  rm -f "$STATE"
fi
echo "[bt-teardown] fin"
