FROM debian:bullseye-slim

# bullseye (glibc 2.31) en vez de bookworm (glibc 2.36): a partir de
# glibc 2.34 pthreads se fusiono dentro de libc.so, lo que hace que
# cualquier binario enlazado contra SDL2 (usa pthreads) pida simbolos
# "GLIBC_2.34" que la consola (glibc 2.33) no tiene.
ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu \
        make \
        file \
        libsdl2-dev:arm64 \
        libsdl2-ttf-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["make"]
