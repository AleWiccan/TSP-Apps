#!/bin/sh
# bt-autoyes.sh <agent.log> <fifo> <autoyes.log>
# bluetoothctl pregunta "(yes/no)" al emparejar (confirmar codigo, aceptar
# emparejamiento...). La Trimui no tiene a nadie delante del terminal, asi
# que respondemos "yes" automaticamente a cada pregunta nueva.
LOG="$1"; FIFO="$2"; OUT="$3"
handled=0
echo "[autoyes] vigilando $LOG" > "$OUT"
while true; do
  n=$(sed 's/(yes\/no)/~/g' "$LOG" 2>/dev/null | tr -cd '~' | wc -c)
  n=$(( $n + 0 ))
  while [ "$n" -gt "$handled" ]; do
    handled=$(( $handled + 1 ))
    echo "[autoyes] pregunta #$handled -> yes" >> "$OUT"
    printf 'yes\n' > "$FIFO"
  done
  sleep 1
done
