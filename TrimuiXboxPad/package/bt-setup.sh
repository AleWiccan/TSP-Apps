#!/bin/sh
# bt-setup.sh - prepara BlueZ para que la app pueda actuar de periferico HID.
#  1) Reinicia bluetoothd con --compat (crea el socket /var/run/sdp que la
#     app necesita para registrar el servicio HID) y sin el plugin "input"
#     (que ocupa los PSM 0x11/0x13). bt-teardown.sh lo deja como estaba.
#  2) Deja un agente NoInputNoOutput VIVO mientras la app corre (sin agente
#     el emparejamiento desde el PC falla), con nombre "Trimui Gamepad".
# Crea un archivo vacio "no-restart" junto a la app para saltarte el paso 1.

DIR="$(cd "$(dirname "$0")" && pwd)"
STATE=/tmp/xboxpad_bt.state
FIFO=/tmp/xboxpad_btctl.fifo
T=""; command -v timeout >/dev/null 2>&1 && T="timeout 10"
rm -f "$STATE"

# Logs solo si existe debug.on junto a la app (si no, a /dev/null; el log del
# agente va a /tmp porque bt-autoyes.sh necesita leerlo).
if [ -f "$DIR/debug.on" ]; then
  AGENT_LOG="$DIR/agent.log"; BTD_LOG="$DIR/bluetoothd.log"; AY_LOG="$DIR/autoyes.log"
  BTD_OPTS="-n -d -C -P input"
else
  AGENT_LOG=/tmp/xboxpad_agent.log; BTD_LOG=/dev/null; AY_LOG=/dev/null
  BTD_OPTS="-n -C -P input"
fi

echo "[bt-setup] inicio"
if [ -f "$DIR/no-restart" ]; then
  echo "[bt-setup] no-restart presente: no toco bluetoothd"
else
  PID=$(ps 2>/dev/null | grep '[b]luetoothd' | head -n1 | sed 's/^ *//' | cut -d' ' -f1)
  if [ -n "$PID" ] && [ -r "/proc/$PID/cmdline" ]; then
    ORIG=$(tr '\0' ' ' < "/proc/$PID/cmdline")
    echo "$ORIG" > "$STATE"
    echo "[bt-setup] bluetoothd original (pid $PID): $ORIG"
    kill "$PID"; sleep 1
    kill -0 "$PID" 2>/dev/null && kill -9 "$PID" && sleep 1
    BTD=$(echo "$ORIG" | cut -d' ' -f1)
    echo "[bt-setup] arrancando: $BTD $BTD_OPTS"
    "$BTD" $BTD_OPTS > "$BTD_LOG" 2>&1 &
    i=0
    while [ $i -lt 10 ] && [ ! -S /var/run/sdp ]; do sleep 1; i=$((i+1)); done
    if [ -S /var/run/sdp ]; then echo "[bt-setup] OK: existe /var/run/sdp"
    else echo "[bt-setup] ERROR: no aparece /var/run/sdp"; ls -l /var/run /run 2>&1 | head -20; fi
  else
    echo "[bt-setup] no encuentro el proceso bluetoothd"
  fi
fi

# Agente persistente: bluetoothctl lee de una FIFO. Un "sleep" en segundo
# plano mantiene la FIFO abierta para escritura (si se cerrara, bluetoothctl
# recibiria fin de entrada, saldria y el agente desapareceria -- justo lo que
# paso en la version anterior y hacia fallar el emparejamiento).
for f in /tmp/xboxpad_btctl.pid /tmp/xboxpad_sleep.pid /tmp/xboxpad_watch.pid; do
  [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null; rm -f "$f"
done
if command -v bluetoothctl >/dev/null 2>&1; then
  rm -f "$FIFO"; mkfifo "$FIFO"
  bluetoothctl < "$FIFO" > "$AGENT_LOG" 2>&1 &
  echo $! > /tmp/xboxpad_btctl.pid
  sleep 86400 > "$FIFO" &
  echo $! > /tmp/xboxpad_sleep.pid
  sleep 2
  sh "$DIR/bt-autoyes.sh" "$AGENT_LOG" "$FIFO" "$AY_LOG" &
  echo $! > /tmp/xboxpad_watch.pid
  # Los comandos de bluetoothctl son asincronos: hay que darles tiempo uno a
  # uno (si no, "agent NoInputNoOutput" llega antes de que termine
  # "agent off" y el agente se queda sin registrar).
  ctl() { printf '%s\n' "$1" > "$FIFO"; sleep 1; }
  ctl 'power on'
  ctl 'system-alias "Trimui Gamepad"'
  ctl 'agent off'
  sleep 1
  ctl 'agent NoInputNoOutput'
  ctl 'default-agent'
  ctl 'discoverable-timeout 0'
  ctl 'pairable on'
  ctl 'discoverable on'
  if ! $T bluetoothctl show 2>&1 | grep -q "Pairable: yes"; then
    echo "[bt-setup] Pairable seguia en no, reintento"
    ctl 'pairable on'
  fi
  echo "[bt-setup] agente lanzado (ver agent.log)"
  $T bluetoothctl show 2>&1 | grep -E "Alias|Powered|Discoverable|Pairable|Class"
else
  echo "[bt-setup] no hay bluetoothctl"
fi

if [ -f "$DIR/set-class" ] && command -v hciconfig >/dev/null 2>&1; then
  echo "[bt-setup] fijando clase"; $T hciconfig hci0 class 0x000508 2>&1
fi
echo "[bt-setup] fin"
