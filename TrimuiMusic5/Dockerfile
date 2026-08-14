FROM debian:bullseye-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu \
        make \
        file \
        pkg-config \
        git \
        python3 \
        ca-certificates \
        libsdl2-dev:arm64 \
        libsdl2-ttf-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*

# --- FFmpeg estatico minimo (solo decodificacion de audio) ---
# Se compila desde codigo fuente en vez de usar los paquetes libavformat-dev/etc
# del repositorio de Debian: esos paquetes arrastran una cadena enorme de
# dependencias opcionales (systemd/dbus/libselinux/gnutls...) compiladas contra
# la GLIBC de Debian bookworm (>=2.34), que NO es compatible con la GLIBC mas
# antigua del Stock OS de la Trimui y provoca errores en tiempo de ejecucion
# tipo "version 'GLIBC_2.34' not found". Al compilar FFmpeg nosotros mismos,
# estatico y sin dependencias externas, evitamos ese problema por completo:
# el binario final solo depende de la libc del sistema para funciones basicas
# (malloc, pthread, etc.), estables desde hace mas de una decada.
RUN git clone --depth 1 --branch n6.1.1 https://github.com/FFmpeg/FFmpeg.git /opt/ffmpeg-src \
    && cd /opt/ffmpeg-src \
    && ./configure \
        --prefix=/opt/ffmpeg-aarch64 \
        --enable-cross-compile \
        --cross-prefix=aarch64-linux-gnu- \
        --arch=aarch64 \
        --target-os=linux \
        --disable-shared --enable-static --enable-pic \
        --disable-programs --disable-doc --disable-htmlpages --disable-manpages \
        --disable-avdevice --disable-avfilter --disable-postproc --disable-network \
        --disable-zlib --disable-bzlib --disable-lzma --disable-iconv \
        --disable-everything \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swresample \
        --enable-decoder=mp3,mp3float,aac,flac,vorbis,opus,alac,wmav1,wmav2,ape,pcm_s16le,pcm_s16be,pcm_u8,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f64le,pcm_alaw,pcm_mulaw,adpcm_ms,adpcm_ima_wav \
        --enable-parser=mpegaudio,aac,flac,vorbis,opus \
        --enable-demuxer=mp3,wav,mov,flac,ogg,asf,matroska,ape,aac \
        --enable-protocol=file \
        --disable-asm \
    && make -j$(nproc) \
    && make install \
    && rm -rf /opt/ffmpeg-src

WORKDIR /workspace
CMD ["make"]
