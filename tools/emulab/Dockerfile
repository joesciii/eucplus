# EUC+ emulab: digikit's Digitakt II emulator (the real OS, with the real DSP for sound) and a
# browser front panel, all inside one container. Nothing is installed on the host but the image and
# one volume. See tools/emulab/README.md.
#
#   docker build -t digitorsion-emulab tools/emulab
#
# The image holds only tools. The Elektron OS, the emulator builds and their state are made on the
# first run, into the /work volume, from Elektron's own download (hash-checked by setup_env.sh).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    RUSTUP_HOME=/opt/rust CARGO_HOME=/opt/rust \
    UV_INSTALL_DIR=/usr/local/bin UV_CACHE_DIR=/work/cache/uv UV_PYTHON_INSTALL_DIR=/work/cache/python \
    PATH=/opt/rust/bin:/usr/local/bin:/usr/bin:/bin

RUN apt-get update -q && apt-get install -y -q --no-install-recommends \
        ca-certificates curl git unzip rsync build-essential pkg-config python3 python3-venv \
        libasound2-dev poppler-utils gcc-m68k-linux-gnu binutils-m68k-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# Rust (the emulator) and uv (digikit's Python), into the image only
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain 1.97.0 --no-modify-path \
    && curl -LsSf https://astral.sh/uv/0.11.32/install.sh | sh

COPY entrypoint.sh /usr/local/bin/emulab
RUN chmod +x /usr/local/bin/emulab
EXPOSE 8080
VOLUME ["/work"]
ENTRYPOINT ["emulab"]
CMD ["start"]
