# Cross-compilación para Trimui Smart Pro (aarch64).
# IMPORTANTE: debian:bullseye-slim (glibc 2.31). La consola tiene glibc < 2.34;
# con bookworm (glibc 2.36) el binario falla con "GLIBC_2.34 not found".
FROM debian:bullseye-slim

ENV DEBIAN_FRONTEND=noninteractive

# Bullseye esta archivado: apuntar apt a archive.debian.org
RUN rm -f /etc/apt/sources.list.d/* \
 && printf 'deb http://archive.debian.org/debian bullseye main\ndeb http://archive.debian.org/debian-security bullseye-security main\n' > /etc/apt/sources.list \
 && printf 'Acquire::Check-Valid-Until "false";\nAcquire::Retries "5";\n' > /etc/apt/apt.conf.d/99archive

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
        libbluetooth-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["make"]
