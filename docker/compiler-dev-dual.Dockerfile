FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    clang \
    cmake \
    git \
    qemu-user-static \
    gcc-riscv64-linux-gnu \
    gcc-aarch64-linux-gnu \
    libc6-dev-riscv64-cross \
    libc6-dev-arm64-cross \
    binutils-riscv64-linux-gnu \
    binutils-aarch64-linux-gnu \
    time \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
