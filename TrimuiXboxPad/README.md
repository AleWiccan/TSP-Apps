# TrimuiXboxPad — Trimui Smart Pro como gamepad Bluetooth

Convierte la Trimui Smart Pro en un **mando Bluetooth HID genérico**:
la consola se empareja directamente con un PC, Mac, Android o Linux y
aparece como un joystick con 2 sticks, 2 gatillos, D-Pad y 12 botones.
No necesita WiFi ni ningún programa adicional en el PC.

## ⚠️ Léelo antes de compilar: qué es y qué NO es esto

Este proyecto **no puede** hacer que Windows reconozca la Trimui como
un "Xbox Wireless Controller" real ni que funcione con **XInput**.
Los mandos de Xbox usan un protocolo propietario de Microsoft (GIP,
*Game Input Protocol*) que corre sobre silicio con licencia — no es
algo que se pueda reproducir con el perfil Bluetooth HID estándar,
sin importar qué VID/PID se declaren.

Lo que **sí** hace: emula el perfil clásico **Bluetooth HID** (el
mismo que usan ratones y teclados Bluetooth) declarando un
descriptor de "Gamepad". Windows lo mostrará en el Panel de
Control → Dispositivos e impresoras → Configuración de
controladores de juego, funcionará en Steam (Steam Input lo detecta
como mando genérico), en emuladores, y en cualquier juego que
soporte DirectInput. **No** funcionará en juegos que exijan
específicamente XInput y no tengan soporte DirectInput alternativo.

Si en algún momento quieres el otro camino (mando XInput real, vía un
programita receptor en el PC con ViGEmBus), es una arquitectura
distinta — avísame y lo armamos aparte.

## Qué NO pude verificar (sé honesto contigo sobre los riesgos)

No tengo una Trimui física ni acceso a su Stock OS, así que este
código está escrito con cuidado sobre la API real de BlueZ pero
**no ha sido compilado ni probado en el hardware real**. Los puntos
de mayor riesgo, de más a menos probable:

1. **Que `bluetoothd` (BlueZ) esté presente y sea suficientemente
   reciente** en el Stock OS. Es muy probable que sí — la consola ya
   soporta emparejar auriculares y mandos Bluetooth como *host*, lo
   que requiere BlueZ — pero no puedo confirmar la versión exacta ni
   si `sdptool`/`bluetoothctl`/`btmgmt` están todos instalados.
2. **Conflicto de puertos (PSM) con el plugin `input` de `bluetoothd`**
   (explicado en `bt-setup.sh` y más abajo).
3. **Permisos root**: hacer `bind()` en un socket L2CAP con PSM < 0x1000
   requiere privilegios de administrador. Si `launch.sh` no corre como
   root en el Stock OS, el binario fallará al arrancar el servicio HID
   (pero seguirá mostrando la UI del mando, para que puedas depurar).
4. **Comportamiento de Windows al emparejar** dispositivos HID hechos
   a mano: algunos hosts son estrictos con las transacciones del canal
   de control (`SET_PROTOCOL`, `GET_REPORT`). Incluí una respuesta
   mínima a esas transacciones (ver `src/bt_hid.c`), pero es la parte
   más probable de necesitar ajustes tras una primera prueba real.

Trátalo como una **primera versión de trabajo**, no como un producto
terminado: es muy probable que necesites 1-2 rondas de depuración con
la consola real en la mano (revisando `stdout`/`stderr` del binario,
que puedes ver conectando por SSH/telnet si el Stock OS lo permite, o
redirigiendo la salida a un fichero de log en el propio `launch.sh`).

## Estructura del proyecto

```
TrimuiXboxPad/
├── Dockerfile              # Cross-compilación (debian:bullseye-slim + libbluetooth)
├── Makefile                # Compila y empaqueta
├── src/
│   ├── main.c              # UI SDL2 + lectura del mando + bucle principal
│   ├── bt_hid.c / .h        # SDP + sockets L2CAP en bruto (rol "device" HID)
│   └── hid_descriptor.h    # Descriptor de reporte HID del "Gamepad"
└── package/
    ├── launch.sh            # Arranque en el Stock OS
    ├── bt-setup.sh          # Configura el adaptador BT antes de lanzar el binario
    ├── config.json          # Metadatos para el launcher
    └── fonts/               # Aquí va tu font.ttf (no incluida)
```

## Cómo compilar

Igual que en tu pre-prompt:

```powershell
docker build -t trimui-sdk .
docker run --rm -v ${PWD}:/workspace trimui-sdk make
```

Esto genera `build/TrimuiXboxPad/` con el binario, `launch.sh`,
`bt-setup.sh` y `config.json`. **Antes de copiarlo a la SD**, añade tú
mismo:

- `build/TrimuiXboxPad/fonts/font.ttf` → cualquier `.ttf` (p. ej.
  `NotoSans-Bold.ttf`).
- `build/TrimuiXboxPad/icon.png` → un icono cuadrado (128×128 o
  similar) para el launcher.

Verifica que el binario sea aarch64 (la salida de `make` ya lo hace
automáticamente con `file`):

```
build/TrimuiXboxPad/TrimuiXboxPad: ELF 64-bit LSB executable, ARM aarch64, ...
```

## Instalación en la consola

Copia toda la carpeta `build/TrimuiXboxPad/` a `SDCARD/Apps/` de la
Trimui, tal como describe tu pre-prompt.

## Cómo emparejar desde Windows

1. Abre la app en la Trimui. Debería mostrar "Esperando
   emparejamiento..." y en `stdout` (si tienes forma de verlo) el
   mensaje `[bt_hid] Escuchando en PSM 0x11...`.
2. En Windows: **Configuración → Bluetooth y dispositivos → Agregar
   dispositivo → Bluetooth**. Debería aparecer **"Trimui Gamepad"**.
3. Selecciónalo. Como el agente es `NoInputNoOutput`, el
   emparejamiento debería completarse sin pedir ningún PIN
   (*Just Works*).
4. Tras emparejar, Windows debería conectar solo los canales de
   control e interrupción. La pantalla de la Trimui pasará a mostrar
   "Conectado a AA:BB:CC:DD:EE:FF".
5. Prueba en `joy.cpl` (Panel de control clásico de controladores de
   juego) o en la página `about:gpu`/gamepad tester de Chrome
   (`chrome://gamepad`) para ver los ejes y botones en vivo.

Para salir de la app en la consola: **SELECT + START** a la vez.

## Mapeo de botones

Los botones se leen como `SDL_Joystick` en bruto (indices fisicos medidos
en la consola) y se traducen con `buttons.ini` (editable en la SD, sin
recompilar). El mapeo de `SDL_GameController` de la consola no es fiable
para los botones (A/B cruzados, Y/L1/Select/Start desplazados), asi que
solo se usa para los ejes, los gatillos y el D-Pad.

| Fisico Trimui (indice) | Boton HID |
|---|---|
| 1 / 0 / 2 / 3 | A / B / X / Y (botones HID 1, 2, 4, 5) |
| 4 / 5 | LB / RB (botones HID 7, 8) |
| 6 / 7 | BACK(Select) / START (botones HID 11, 12) |
| 8 (Menu) | GUIDE (boton HID 13) |
| D-Pad | Hat Switch (POV) |
| Sticks / gatillos | Ejes X/Y, Rx/Ry y Z/Rz |

Los botones HID siguen el orden estandar de gamepad de Android/Linux (1=A 2=B 3=C 4=X 5=Y 6=Z 7=L1 8=R1 9=L2 10=R2 11=Select 12=Start 13=Mode 14=L3 15=R3), el mismo (con huecos) del mando Xbox por Bluetooth; por eso en `joy.cpl` los botones 3, 6, 9 y 10 no se usan.

Cada pulsacion queda en `log.txt` como `boton fisico N pulsado -> X`.

## Logs (desactivados por defecto)

Para depurar, crea un archivo vacio `debug.on` junto a la app en la SD:
se escribiran `log.txt`, `agent.log`, `bluetoothd.log` y `autoyes.log` en la
carpeta. Sin ese archivo no se escribe nada (y se borran los logs viejos).

## Problemas conocidos y su causa probable

| Síntoma | Causa probable | Qué probar |
|---|---|---|
| El binario no arranca / sale enseguida | Falta `libbluetooth.so` en el dispositivo o `launch.sh` no corre como root | Ejecutar el binario a mano por SSH y mirar el `stderr` |
| `bind PSM 0x11 fallo: Address already in use` | El plugin `input` de `bluetoothd` ya tiene ocupado ese puerto | Añadir `DisablePlugins = input` en `/etc/bluetooth/main.conf` y reiniciar `bluetoothd` (ver `bt-setup.sh`) |
| `bind PSM ... fallo: Permission denied` | El proceso no corre como root | Revisar cómo lanza procesos el Stock OS; puede requerir `su`/`sudo` en `launch.sh` |
| No aparece "Trimui Gamepad" al escanear desde Windows | Adaptador no discoverable, o `bluetoothctl`/`hciconfig` no existen en el Stock OS | Ejecutar `bt-setup.sh` a mano y revisar sus AVISOs; probar `bluetoothctl show` |
| Empareja pero Windows lo marca como "Emparejado" sin más, sin reconocerlo como joystick | El SDP record no llegó a registrarse bien (revisa `stdout` del binario) o falta responder `SET_PROTOCOL` | Revisar logs; probar con `sdptool browse <MAC>` desde otro Linux para inspeccionar el Service Record anunciado |
| Se conecta pero los ejes/botones no se mueven en `joy.cpl` | Descriptor HID mal interpretado por el driver genérico de Windows, o `BuildReport()` no coincide con el orden de bits del descriptor | Revisar que `hid_descriptor.h` y `BuildReport()` en `main.c` sigan siendo consistentes si los tocas |
