# Quick Start Recipes

## Checking out the Zircon source code

*** note
NOTE: The Fuchsia source includes Zircon. See Fuchsia's
[Getting Started](https://fuchsia.googlesource.com/docs/+/master/getting_started.md)
doc. Follow this doc to work on only Zircon.
***

The Zircon Git repository is located
at: https://fuchsia.googlesource.com/zircon

To clone the repository, assuming you setup the $SRC variable
in your environment:
```shell
git clone https://fuchsia.googlesource.com/zircon $SRC/zircon
```

For the purpose of this document, we will assume that Zircon is checked
out in $SRC/zircon and that we will build toolchains, QEMU, etc alongside
that.  Various make invocations are presented with a "-j32" option for
parallel make.  If that's excessive for the machine you're building on,
try -j16 or -j8.

## Preparing the build environment

### Ubuntu

On Ubuntu this should obtain the necessary pre-reqs:
```
sudo apt-get install texinfo libglib2.0-dev autoconf libtool bison libsdl-dev build-essential
```

### macOS
Install the Xcode Command Line Tools:
```
xcode-select --install
```

Install the other pre-reqs:

* Using Homebrew:
```
brew install wget pkg-config glib autoconf automake libtool
```

* Using MacPorts:
```
port install autoconf automake libtool libpixman pkgconfig glib2
```

## Install Toolchains

If you're developing on Linux or macOS, there are prebuilt toolchain binaries avaiable.
Just run this script from your Zircon working directory:

```
./scripts/download-prebuilt
```

If you would like to build the toolchains yourself, follow the instructions later
in the document.

## Build Zircon

Build results will be in $SRC/zircon/build-{arm64,x64}

The variable $BUILDDIR in examples below refers to the build output directory
for the particular build in question.

```
cd $SRC/zircon

# for aarch64
make -j32 arm64

# for x64
make -j32 x64
```

### Using Clang

To build Zircon using Clang as the target toolchain, set the
`USE_CLANG=true` variable when invoking Make.

```
cd $SRC/zircon

# for aarch64
make -j32 USE_CLANG=true arm64

# for x64
make -j32 USE_CLANG=true x64
```

## Building Zircon for all targets

```
# The -r enables release builds as well
./scripts/buildall -r
```

Please build for all targets before submitting to ensure builds work
on all architectures.

## QEMU

You can skip this if you're only testing on actual hardware, but the emulator
is handy for quick local tests and generally worth having around.

See [QEMU](qemu.md) for information on building and using QEMU with zircon.


## Build Toolchains (Optional)

If the prebuilt toolchain binaries do not work for you, you can build your
own from vanilla upstream sources.

 * The GCC toolchain is used to build Zircon by default.
 * The Clang toolchain is used to build Zircon if you build with
   `USE_CLANG=true` or `USE_ASAN=true`.
 * The Clang toolchain is also used by default to build host-side code, but
   any C++14-capable toolchain for your build host should work fine.

Build one or the other or both, as needed for how you want build Zircon.

### GCC Toolchain

We use GNU `binutils` 2.30(`*`) and GCC 8.2(`**`), configured with
`--enable-initfini-array --enable-gold`, and with `--target=x86_64-elf
--enable-targets=x86_64-pep` for x86-64 or `--target=aarch64-elf` for ARM64.

For `binutils`, we recommend `--enable-deterministic-archives` but that switch
is not necessary to get a working build.

For GCC, it's necessary to pass `MAKEOVERRIDES=USE_GCC_STDINT=provide` on the
`make` command line.  This should ensure that the `stdint.h` GCC installs is
one that works standalone (`stdint-gcc.h` in the source) rather than one that
uses `#include_next` and expects another `stdint.h` file installed elsewhere.

Only the C and C++ language support is required and no target libraries other
than `libgcc` are required, so you can use various `configure` switches to
disable other things and make your build of GCC itself go more quickly and use
less storage, e.g. `--enable-languages=c,c++ --disable-libstdcxx
--disable-libssp --disable-libquadmath`.  See the GCC installation
documentation for more details.

You may need various other `configure` switches or other prerequisites to
build on your particular host system.  See the GNU documentation.

(`*`) The `binutils` 2.30 release has some harmless `make check` failures in
the `aarch64-elf` and `x86_64-elf` configurations.  These are fixed on the
upstream `binutils-2_30-branch` git branch, which is what we actually build.
But the 2.30 release version works fine for building Zircon; it just has some
spurious failures in its own test suite.

(`**`) As of 2008-6-15, GCC 8.2 has not been released yet.  There is no
released version of GCC that works for building Zircon without backporting
some fixes.  What we actually use is the upstream `gcc-8-branch` git branch.

### Clang/LLVM Toolchain

We use a trunk snapshot of Clang and update to new snapshots frequently.  Any
build of recent-enough Clang with support for `x86_64` and `aarch64` compiled
in should work.  You'll need a toolchain that also includes the runtime
libraries.  We normally also use the same build of Clang for the host as well
as for the `*-fuchsia` targets.  See
[here](https://fuchsia.googlesource.com/docs/+/master/development/build/toolchain.md)
for details on how we build Clang.

### Set up `local.mk` for toolchains

If you're using the prebuilt toolchains, you can skip this step, since
the build will find them automatically.

Create a GNU makefile fragment in `local.mk` that points to where you
installed the toolchains:

```makefile
CLANG_TOOLCHAIN_PREFIX := .../clang-install/bin/
ARCH_x86_64_TOOLCHAIN_PREFIX := .../gnu-install/bin/x86_64-elf-
ARCH_arm64_TOOLCHAIN_PREFIX := .../gnu-install/bin/aarch64-elf-
```

Note that `CLANG_TOOLCHAIN_PREFIX` should have a trailing slash, and the
`ARCH_*_TOOLCHAIN_PREFIX` variables for the GNU toolchains should include the
`${target_alias}-` prefix, so that simple command names like `gcc`, `ld`, or
`clang` can be appended to the prefix with no separator.  If the `clang` or
`gcc` in your `PATH` works for Zircon, you can just use empty prefixes.

## Copying files to and from Zircon

With local link IPv6 configured, the host tool ./build-ARCH/tools/netcp
can be used to copy files.

```
# Copy the file myprogram to Zircon
netcp myprogram :/tmp/myprogram

# Copy the file myprogram back to the host
netcp :/tmp/myprogram myprogram
```

## Including Additional Userspace Files

The Zircon build creates a bootfs image containing necessary userspace components
for the system to boot (the device manager, some device drivers, etc).  The kernel
is capable of including a second bootfs image which is provided by QEMU or the
bootloader as a ramdisk image.

To create such a bootfs image, use the zbi tool that's generated as part of
the build.  It can assemble a bootfs image for either source directories (in which
case every file in the specified directory and its subdirectories are included) or
via a manifest file which specifies on a file-by-file basis which files to include.

```
$BUILDDIR/tools/zbi -o extra.bootfs @/path/to/directory

echo "issue.txt=/etc/issue" > manifest
echo "etc/hosts=/etc/hosts" >> manifest
$BUILDDIR/tools/zbi -o extra.bootfs manifest
```

On the booted Zircon system, the files in the bootfs will appear under /boot, so
in the above manifest example, the "hosts" file would appear at /boot/etc/hosts.

For QEMU, use the -x option to the run-zircon-* scripts to specify an extra bootfs image.

## Network Booting

Network booting is supported via two mechanisms: Gigaboot and Zirconboot.
Gigaboot is an EFI based bootloader whereas zirconboot is a mechanism that
allows a minimal zircon system to serve as a bootloader for zircon.

On systems that boot via EFI (such as Acer and NUC), either option is viable.
On other systems, zirconboot may be the only option for network booting.

### Via Gigaboot
The [GigaBoot20x6](https://fuchsia.googlesource.com/zircon/+/master/bootloader) bootloader speaks a simple network boot protocol (over IPV6 UDP)
which does not require any special host configuration or privileged access to use.

It does this by taking advantage of IPV6 Link Local Addressing and Multicast,
allowing the device being booted to advertise its bootability and the host to find
it and send a system image to it.

If you have a device (for example a Broadwell or Skylake Intel NUC) running
GigaBoot20x6 first create a USB drive [manually](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md#How-to-Create-a-Bootable-USB-Flash-Drive)
or (Linux only) using the [script](https://fuchsia.googlesource.com/scripts/+/master/build-bootable-usb-gigaboot.sh).

```
$BUILDDIR/tools/bootserver $BUILDDIR/zircon.bin

# if you have an extra bootfs image (see above):
$BUILDDIR/tools/bootserver $BUILDDIR/zircon.bin /path/to/extra.bootfs
```

By default bootserver will continue to run and every time it obsveres a netboot
beacon it will send the kernel (and bootfs if provided) to that device.  If you
pass the -1 option, bootserver will exit after a successful boot instead.


### Via Zirconboot
Zirconboot is a mechanism that allows a zircon system to serve as the
bootloader for zircon itself. Zirconboot speaks the same boot protocol as
Gigaboot described above.

To use zirconboot, pass the `netsvc.netboot=true` argument to zircon via the
kernel command line. When zirconboot starts, it will attempt to fetch and boot
into a zircon system from a bootserver running on the attached host.

## Network Log Viewing

The default build of Zircon includes a network log service that multicasts the
system log over the link local IPv6 UDP.  Please note that this is a quick hack
and the protocol will certainly change at some point.

For now, if you're running Zircon on QEMU with the -N flag or running on hardware
with a supported ethernet interface (ASIX USB Dongle or Intel Ethernet on NUC),
the loglistener tool will observe logs broadcast over the local link:

```
$BUILDDIR/tools/loglistener
```

## Debugging

For random tips on debugging in the zircon environment see
[debugging](debugging/tips.md).

## Contribute changes
* See [contributing.md](contributing.md).
