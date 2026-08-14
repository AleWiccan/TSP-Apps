# FTP GUI — Trimui Smart Pro

Interfaz grafica en C + SDL2 para el servicio FTP (`tcpsvd` + `ftpd` de busybox):

- Muestra la IP actual del dispositivo.
- Botón para iniciar / detener el servicio FTP.
- Pantalla para cambiar la contraseña del usuario `root` (usado por `ftpd` para
  autenticar), con teclado en pantalla navegable por D-Pad.
- El servicio FTP, una vez iniciado, **sigue corriendo en segundo plano** aunque
  cierres la app (se lanza desacoplado con `setsid()` + doble fork, así el
  Stock OS no lo mata al volver al menú).
- Tema visual con acentos morados.

## 1. Antes de compilar: pon una fuente TTF

Descarga `NotoSans-Bold.ttf` (o cualquier `.ttf`) y colócala en:

```
fonts/font.ttf
```

Si no la pones, `make package` avisará y la app buscará una fuente del
sistema como respaldo (puede no existir en el dispositivo).

## 2. Compilar (Windows + Docker Desktop)

Desde la carpeta del proyecto, en PowerShell:

```powershell
# 1. Construir la imagen (solo la primera vez, o si cambia el Dockerfile)
docker build -t trimui-sdk .

# 2. Compilar el binario aarch64
docker run --rm -v ${PWD}:/workspace trimui-sdk make

# 3. Empaquetar la app para la SD
docker run --rm -v ${PWD}:/workspace trimui-sdk make package
```

Verifica que el binario sea ARM (no x86_64):

```powershell
docker run --rm -v ${PWD}:/workspace trimui-sdk file build/ftpgui
# Debe decir: ELF 64-bit LSB executable, ARM aarch64
```

Verifica también que no pida una version de GLIBC mas nueva que la de la
consola (2.33). La ultima version listada debe ser 2.33 o menor:

```powershell
docker run --rm -v ${PWD}:/workspace trimui-sdk aarch64-linux-gnu-objdump -T build/ftpgui
# Filtrar en la salida las lineas con "GLIBC_" y revisar la mas alta
```

Si en algun momento vuelves a ver un error de "GLIBC_2.34 not found" (u
otra version superior a 2.33) en la consola, significa que la imagen Docker
usada para compilar trae una glibc demasiado nueva. Ya se ajusto este
proyecto para usar `debian:bullseye-slim` (glibc 2.31) en vez de
`bookworm-slim` (glibc 2.36) precisamente por esto: desde glibc 2.34,
`pthread_*` se fusiono dentro de `libc.so`, y SDL2 usa pthreads
internamente, asi que cualquier build contra una glibc >= 2.34 arrastra
esos simbolos nuevos aunque tu codigo no llame pthreads directamente.

## 3. Instalar en la consola

Copia la carpeta generada a la tarjeta SD:

```
SDCARD/Apps/FTP-GUI/
├── launch.sh
├── ftpgui
├── config.json
├── icon.png
└── fonts/font.ttf
```

Todo esto ya queda armado dentro de `SDCARD/Apps/FTP-GUI/` tras `make package`.
Solo copia esa carpeta a `Apps/` en la SD de la consola.

## 4. Uso en la consola

- **D-Pad**: navegar
- **A**: confirmar / seleccionar
- **B**: atrás / borrar (en pantalla de contraseña)
- **Y**: mostrar/ocultar contraseña
- **START**: guardar contraseña (atajo, también hay tecla "GUARDAR" en el teclado)
- **SELECT**: cancelar / salir de la app

Al iniciar el servicio FTP, conéctate desde tu PC a `ftp://<IP-mostrada>:21`
con usuario `root` y la contraseña configurada (por defecto, la que ya tenga
el sistema — cámbiala desde la app si quieres una nueva).

## Notas de diseño

- Se eliminó el auto-apagado por inactividad (300s) del script original,
  porque ahora el encendido/apagado es manual desde la GUI.
- El cambio de contraseña actúa sobre el usuario `root`, igual que hacía el
  script original (`passwd root`), ya que es contra ese usuario que
  autentica `ftpd`.
- El servidor se lanza vía `tcpsvd -vE 0.0.0.0 21 ftpd -w /mnt/SDCARD`,
  igual que en el script original, sirviendo la SD completa.
