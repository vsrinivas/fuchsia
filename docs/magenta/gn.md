# GN

## Prerequisites

1. run `sudo apt-get install ninja-build` to install Ninja

2. build toolchains for aarch64 and x86_64:

   ```bash
   cd $SRC
   git clone https://github.com/travisg/toolchains.git
   cd toolchains
   ./doit -a 'arm i686 aarch64 x86_64' -f -j32
   export PATH=$SRC/toolchains/aarch64-elf-5.3.0-Linux-x86_64/bin:$SRC/toolchains/x86_64-elf-5.3.0-Linux-x86_64/bin:$PATH
   ```

3. build and install qemu:

   ```bash
   cd $SRC
   git clone http://git.qemu.org/git/qemu.git
   cd qemu
   ./configure --target-list=aarch64-softmmu,x86_64-softmmu
   make -j32
   ```

4. run `./scripts/download-gn` (from the lk directory) to download GN

## Build Magenta with GN for QEMU x64

1. run GN to generate Ninja build script:

   ```bash
   ./gn gen out/qemu-x64 --script-executable=/bin/sh --args='target_cpu="x64" target_platform="qemu"'
   ```

2. run Ninja to build the Magenta:

   ```bash
   ninja -C out/qemu-x64
   ```

3. run Magenta using qemu:

   ```bash
   ./scripts/run-magenta-x86-64 -o out/qemu-x64
   ```

## Build Magenta with GN for QEMU arm64

1. run GN to generate Ninja build script:

   ```bash
   ./gn gen out/qemu-arm64 --script-executable=/bin/sh --args='target_cpu="arm64" target_platform="qemu"'
   ```

2. run Ninja to build the Magenta:

   ```bash
   ninja -C out/qemu-arm64
   ```

3. run Magenta using qemu:

   ```bash
   ./scripts/run-magenta-arm64 -o out/qemu-arm64
   ```

## Build Magenta with GN for Pixel 2

1. run GN to generate Ninja build script:

   ```bash
   ./gn gen out/pc-x64 --script-executable=/bin/sh --args='target_cpu="x64" target_platform="pc"'
   ```

2. run Ninja to build the Magenta:

   ```bash
   ninja -C out/pc-x64
   ```

3. run Magenta using network boot:

   ```bash
   ./tools/bootserver out/pc-x64/lk.bin
   ```

## Build Magenta with GN for QEMU arm32

1. run GN to generate Ninja build script:

   ```bash
   ./gn gen out/qemu-arm32 --script-executable=/bin/sh --args='target_cpu="arm32" target_platform="qemu"'
   ```

2. run Ninja to build the Magenta:

   ```bash
   ninja -C out/qemu-arm32
   ```

3. run Magenta using qemu:

   ```bash
   ./scripts/run-magenta-arm32 -o out/qemu-arm32
   ```
