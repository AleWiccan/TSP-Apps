# TrimuiMusic

Reproductor de música nativo para **Trimui Smart Pro** (C + SDL2 + FFmpeg libav).

## Qué incluye

- **Formatos soportados**: MP3, WAV (PCM/ADPCM/A-law/µ-law), M4A/AAC, FLAC, OGG
  Vorbis/Opus, WMA, ALAC, APE, Matroska de audio. Se decodifica con **FFmpeg
  compilado estáticamente desde código fuente** (solo los decoders/demuxers
  necesarios, sin red ni filtros), embebido directamente en el binario — no
  depende de ninguna librería `.so` externa de FFmpeg en tiempo de ejecución,
  así se evita cualquier problema de compatibilidad de GLIBC con el Stock OS
  de la consola (ver sección de solución de problemas más abajo).
- **Reproducción continua** mientras se navega dentro de la app: el audio corre en
  un hilo decodificador independiente del hilo de render/input (no se corta al
  moverte por el explorador de archivos o ver la letra).
- **Visualizador** de barras con FFT (radix-2, 512 muestras) + suavizado de caída,
  con gradiente de color. Se activa/desactiva con el botón **X** (hay un hint en
  pantalla; la consola no tiene touchscreen, así que el "botón en pantalla" se
  controla con el mando físico).
- **No apagar pantalla**: se llama a `SDL_DisableScreenSaver()` al iniciar.
- **Letras `.lrc`**: si existe `NombreCancion.lrc` junto al archivo de audio
  (`NombreCancion.mp3`), se parsea y se muestra sincronizada, resaltando la línea
  actual. Toggle con **Y**.
- Mapeo de botones exactamente como en la tabla del pre-prompt (A/B/X/Y, D-Pad por
  botón, eje y hat, SELECT para salir).

## Estructura

```
trimui-music-player/
├── Dockerfile          (toolchain aarch64 + SDL2 + libav*)
├── Makefile
├── launch.sh
├── fonts/              (coloca aquí NotoSans-Bold.ttf antes de empaquetar)
└── src/
    ├── main.c          bucle principal, pantallas (explorador/reproductor)
    ├── decoder.c/.h     decodificación FFmpeg -> PCM S16 44.1kHz estéreo
    ├── ringbuffer.c/.h  buffer circular thread-safe entre decoder y audio_out
    ├── audio_out.c/.h   salida SDL2 (callback) + snapshot para el visualizador
    ├── visualizer.c/.h  FFT + barras
    ├── lyrics.c/.h      parser y render de .lrc
    ├── browser.c/.h     explorador de archivos/carpetas
    └── input.c/.h       mapeo de botones/joystick
```

## Compilar (Windows + Docker Desktop)

```powershell
cd trimui-music-player
docker build -t trimui-sdk .
docker run --rm -v ${PWD}:/workspace trimui-sdk make
docker run --rm -v ${PWD}:/workspace trimui-sdk make package
```

> **Nota:** `docker build` ahora compila FFmpeg estático desde código fuente
> dentro de la imagen (solo la primera vez, o si borras la imagen/caché de
> Docker). Puede tardar entre 5 y 15 minutos según tu máquina. Las siguientes
> veces que corras `docker build` sin cambiar el `Dockerfile`, Docker reutiliza
> la capa cacheada y es instantáneo.

Verifica el binario:
```powershell
docker run --rm -v ${PWD}:/workspace trimui-sdk file build/TrimuiMusic
# Debe decir: ELF 64-bit LSB executable, ARM aarch64
```

Antes de copiar a la SD, coloca `NotoSans-Bold.ttf` en:
`dist/TrimuiMusic.pak/fonts/NotoSans-Bold.ttf`

Luego copia toda la carpeta `dist/TrimuiMusic.pak/` a `SDCARD/Apps/` en la
tarjeta de la consola.

## Carpeta de música

Por defecto busca en `/mnt/SDCARD/Music`. Está definido en `main.c`:
```c
#define MUSIC_DIR_DEFAULT "/mnt/SDCARD/Music"
```
Cámbialo si tu organización de carpetas es distinta y recompila.

## Limitaciones honestas (para que no haya sorpresas)

1. **"Segundo plano" real de OS**: el Stock OS de estas consolas normalmente
   ejecuta **una sola app en primer plano** a la vez (no hay multitarea real como
   en un smartphone). Lo que esta app sí garantiza es que el audio **no se corta
   al navegar dentro de la propia app** (explorador, letras, visualizador), porque
   decodificación y render van en hilos separados. Si necesitas que la música siga
   sonando al volver al menú principal de la consola, eso dependería de un daemon
   a nivel de sistema fuera del alcance de una app individual — no está garantizado
   por el firmware stock.
2. **No-sleep**: `SDL_DisableScreenSaver()` es la API estándar y debería evitar el
   salvapantallas de SDL, pero si el Stock OS tiene su propio daemon de auto-apagado
   basado en inactividad de botones (independiente de SDL), puede que sea necesario
   un ajuste adicional específico del firmware que no está documentado públicamente.
   Pruébalo en tu unidad; si se sigue apagando, dime qué mecanismo usa tu firmware
   (archivo de configuración, script en `/etc/init.d`, etc.) y lo ajustamos.
3. El visualizador está calibrado para bajo consumo (FFT de 512 puntos, ~60fps con
   vsync, cálculo solo si está visible) pero en un SoC de 4 núcleos A53 conviene
   probarlo con la app real para afinar si hace falta bajar el tamaño de FFT o el fps.

## Controles

| Botón | Explorador | Reproductor |
|---|---|---|
| D-Pad arriba/abajo | Mover selección | — |
| A | Entrar/reproducir | Pausa/reanudar |
| B | Subir carpeta | Volver al explorador |
| X | — | Mostrar/ocultar visualizador |
| Y | — | Mostrar/ocultar letra |
| Izq/Der | — | Retroceder/avanzar 10s |
| SELECT | Salir | Salir |

## Solución de problemas

### "version 'GLIBC_2.3X' not found" (en FFmpeg o en el propio TrimuiMusic)

La GLIBC del Stock OS de la consola es más vieja que la de Debian bookworm
(2.36), así que el `Dockerfile` ahora compila **todo** — nuestro código y
FFmpeg — contra **Debian Bullseye** (GLIBC 2.31, de 2021) en vez de bookworm.
Esto cubre los casos que ya vimos (`GLIBC_2.34` de una dependencia de FFmpeg,
`GLIBC_2.35` de `libm` en nuestro propio binario).

Si después de recompilar con esta versión **todavía** aparece un error de
`GLIBC_2.3X not found` (poco probable, pero la GLIBC exacta de tu consola es
un dato que no tengo con certeza), la forma más rápida de resolverlo de una
vez es que me confirmes la versión exacta que corre tu consola. Si tienes
acceso por SSH a la Trimui, corre esto directamente en la consola:
```sh
/lib64/libc.so.6 --version
# o, si eso no imprime nada útil:
strings /lib64/libc.so.6 | grep "^GLIBC_2\." | sort -V | tail -1
```
Con ese dato puedo fijar la imagen base de Debian exacta (o incluso una más
vieja como `buster` o `stretch`) que garantice compatibilidad total sin más
vueltas de prueba y error.

### "error while loading shared libraries: libavformat.so.XX"

Esto ya no debería ocurrir con esta versión del proyecto: FFmpeg se compila
**estático** (embebido dentro del propio binario `TrimuiMusic`), así que no
depende de ningún `.so` externo de FFmpeg en tiempo de ejecución.

Si compilaste con una versión anterior de este proyecto (la que usaba
`libavformat-dev:arm64` vía `apt`), borra `dist/` y la imagen Docker vieja y
reconstruye desde cero:
```powershell
docker rmi trimui-sdk
docker build -t trimui-sdk .
docker run --rm -v ${PWD}:/workspace trimui-sdk make clean
docker run --rm -v ${PWD}:/workspace trimui-sdk make package
```

Lo único que sigue siendo dinámico es **SDL2** y **SDL2_ttf**, que la consola
ya trae instalados de fábrica en `/usr/trimui/lib` (documentado y comprobado
con builds anteriores que sí funcionaron), así que no deberían dar este tipo
de problema.

Por seguridad, `make package` sigue corriendo `scripts/collect_libs.sh`, que
copiaría a `TrimuiMusic.pak/lib/` cualquier otra librería no estándar que el
binario llegara a necesitar — con el build estático normalmente no debería
copiar nada.

### "Clock skew detected" / "modification time in the future"

Advertencia inofensiva de `make` por diferencia de reloj entre tu Windows y el
contenedor Docker Desktop (WSL2). No afecta el binario generado.
