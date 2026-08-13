#!/bin/bash
# Copia recursivamente las librerías compartidas (.so) que el binario aarch64
# necesita en tiempo de ejecución, EXCLUYENDO las que ya provee el Stock OS
# de la Trimui (libc, SDL2, etc. en /usr/trimui/lib).
#
# Uso: collect_libs.sh <binario> <carpeta_destino>

set -e
BIN="$1"
OUTDIR="$2"

if [ -z "$BIN" ] || [ -z "$OUTDIR" ]; then
    echo "Uso: $0 <binario> <carpeta_destino>"
    exit 1
fi

SEARCH_DIRS="/usr/lib/aarch64-linux-gnu /lib/aarch64-linux-gnu"

# Prefijos de librerías que NO se deben empaquetar porque ya las trae
# el firmware de la consola (o son parte del sistema base de Linux).
SKIP_PREFIXES="libc.so libm.so libpthread.so libdl.so librt.so libresolv.so \
ld-linux libSDL2 libgcc_s.so"

mkdir -p "$OUTDIR"
declare -A seen

should_skip() {
    local base="$1"
    for skip in $SKIP_PREFIXES; do
        case "$base" in
            "$skip"*) return 0 ;;
        esac
    done
    return 1
}

resolve_path() {
    local libname="$1"
    for d in $SEARCH_DIRS; do
        if [ -e "$d/$libname" ]; then
            echo "$d/$libname"
            return 0
        fi
    done
    return 1
}

process() {
    local given_path="$1"
    local real_path
    real_path=$(readlink -f "$given_path")
    local real_base
    real_base=$(basename "$real_path")
    local given_base
    given_base=$(basename "$given_path")

    if [ -n "${seen[$real_base]}" ]; then
        return
    fi
    seen[$real_base]=1

    # Comprobar exclusion contra AMBOS nombres: el solicitado (p.ej. "libc.so.6",
    # el que aparece en NEEDED) y el nombre real del archivo en disco. En GLIBC
    # anteriores a 2.34 el archivo real se llama "libc-2.31.so" (no "libc.so*"),
    # asi que solo chequear el nombre real dejaba pasar libc/libpthread/librt
    # por error -- eso rompe el sistema en el dispositivo (mezcla de dos libc
    # distintas en el mismo proceso).
    if should_skip "$given_base" || should_skip "$real_base"; then
        return
    fi

    cp -Lf "$real_path" "$OUTDIR/$real_base"

    if [ "$given_base" != "$real_base" ]; then
        # Copiamos el archivo real bajo el nombre "dado" tambien, en vez de
        # crear un symlink: la SD de la consola suele ser FAT32/exFAT, que
        # NO soporta symlinks de Unix, y el enlace se rompe al copiarlo desde
        # Windows.
        cp -Lf "$real_path" "$OUTDIR/$given_base"
    fi

    local needed
    needed=$(aarch64-linux-gnu-objdump -p "$real_path" 2>/dev/null | awk '/NEEDED/{print $2}')
    for n in $needed; do
        local path
        if path=$(resolve_path "$n"); then
            process "$path"
        fi
    done
}

top_needed=$(aarch64-linux-gnu-objdump -p "$BIN" | awk '/NEEDED/{print $2}')
for n in $top_needed; do
    if path=$(resolve_path "$n"); then
        process "$path"
    else
        echo "AVISO: no se encontro $n en el sysroot, revisa manualmente" >&2
    fi
done

echo "Librerias copiadas a $OUTDIR:"
ls -la "$OUTDIR"
