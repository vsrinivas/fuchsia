# Quick Start Recipes

## Checking out the Magenta source code

*** note
NOTE: The Fuchsia source includes Magenta. See Fuchsia's
[Getting Started](https://fuchsia.googlesource.com/docs/+/master/getting_started.md)
doc. Follow this doc to work on only Magenta.
***

The Magenta Git repository is located
at: https://fuchsia.googlesource.com/magenta

To clone the repository, assuming you setup the $SRC variable
in your environment:
```shell
git clone https://fuchsia.googlesource.com/magenta $SRC/magenta
```

For the purpose of this document, we will assume that Magenta is checked
out in $SRC/magenta and that we will build toolchains, QEMU, etc alongside
that.  Various make invocations are presented with a "-j32" option for
parallel make.  If that's excessive for the machine you're building on,
try -j16 or -j8.

## Preparing the build environment

### Ubuntu

On Ubuntu this should obtain the necessary pre-reqs:
```
sudo apt-get install texinfo libglib2.0-dev autoconf libtool libsdl-dev build-essential
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
Just run this script from your Magenta working directory:

```
./scripts/download-toolchain
```

If you would like to build the toolchains yourself, follow the instructions later
in the document.

## Build Magenta

Build results will be in $SRC/magenta/build-{qemu-arm64,pc-x86-64}

The variable $BUILDDIR in examples below refers to the build output directory
for the particular build in question.

```
cd $SRC/magenta

# for aarch64
make -j32 magenta-qemu-arm64

# for x86-64
make -j32 magenta-pc-x86-64
```

### Using Clang

To build Magenta using Clang as the target toolchain, set the
`USE_CLANG=true` variable when invoking Make.

```
cd $SRC/magenta

# for aarch64
make -j32 USE_CLANG=true magenta-qemu-arm64

# for x86-64
make -j32 USE_CLANG=true magenta-pc-x86-64
```

## Building Magenta for all targets

```
# The -r enables release builds as well
./scripts/buildall -r
```

Please build for all targets before submitting to ensure builds work
on all architectures.

## QEMU

You can skip this if you're only testing on actual hardware, but the emulator
is handy for quick local tests and generally worth having around.

See [QEMU](qemu.md) for information on building and using QEMU with magenta.


## Build Toolchains (Optional)

If the prebuilt toolchain binaries do not work for you, there are a
set of scripts which will download and build suitable gcc toolchains
for building Magenta for the ARM64 and x86-64 architectures:

```
cd $SRC
git clone https://fuchsia.googlesource.com/third_party/gcc_none_toolchains toolchains
cd toolchains.
./do-build --target arm-none
./do-build --target aarch64-none
./do-build --target x86_64-none
```

### Configure PATH for toolchains

If you're using the prebuilt toolchains, you can skip this step, since
the build will find them automatically.

```
# on Linux
export PATH=$PATH:$SRC/toolchains/aarch64-elf-5.3.0-Linux-x86_64/bin
export PATH=$PATH:$SRC/toolchains/x86_64-elf-5.3.0-Linux-x86_64/bin

# on Mac
export PATH=$PATH:$SRC/toolchains/aarch64-elf-5.3.0-Darwin-x86_64/bin
export PATH=$PATH:$SRC/toolchains/x86_64-elf-5.3.0-Darwin-x86_64/bin
```

## Copying files to and from Magenta

With local link IPv6 configured, the host tool ./build-magenta-ARCH/tools/netcp
can be used to copy files.

```
# Copy the file myprogram to Magenta
netcp myprogram :/tmp/myprogram

# Copy the file myprogram back to the host
netcp :/tmp/myprogram myprogram
```

## Including Additional Userspace Files

The Magenta build creates a bootfs image containing necessary userspace components
for the system to boot (the device manager, some device drivers, etc).  The kernel
is capable of including a second bootfs image which is provided by QEMU or the
bootloader as a ramdisk image.

To create such a bootfs image, use the mkbootfs tool that's generated as part of
the build.  It can assemble a bootfs image for either source directories (in which
case every file in the specified directory and its subdirectories are included) or
via a manifest file which specifies on a file-by-file basis which files to include.

```
$BUILDDIR/tools/mkbootfs -o extra.bootfs @/path/to/directory

echo "issue.txt=/etc/issue" > manifest
echo "etc/hosts=/etc/hosts" >> manifest
$BUILDDIR/tools/mkbootfs -o extra.bootfs manifest
```

On the booted Magenta system, the files in the bootfs will appear under /boot, so
in the above manifest example, the "hosts" file would appear at /boot/etc/hosts.

For QEMU, use the -x option to the run-magenta-* scripts to specify an extra bootfs image.

## Network Booting

Network booting is supported via two mechanisms: Gigaboot and Magentaboot.
Gigaboot is an EFI based bootloader whereas magentaboot is a mechanism that
allows a minimal magenta system to serve as a bootloader for magenta.

On systems that boot via EFI (such as Acer and NUC), either option is viable.
On other systems, magentaboot may be the only option for network booting.

### Via Gigaboot
The [GigaBoot20x6](https://fuchsia.googlesource.com/magenta/+/master/bootloader) bootloader speaks a simple network boot protocol (over IPV6 UDP)
which does not require any special host configuration or privileged access to use.

It does this by taking advantage of IPV6 Link Local Addressing and Multicast,
allowing the device being booted to advertise its bootability and the host to find
it and send a system image to it.

If you have a device (for example a Broadwell or Skylake Intel NUC) running
GigaBoot20x6 first create a USB drive [manually](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/acer12.md#How-to-Create-a-Bootable-USB-Flash-Drive)
or (Linux only) using the [script](https://fuchsia.googlesource.com/scripts/+/master/build-bootable-usb-gigaboot.sh).

```
$BUILDDIR/tools/bootserver $BUILDDIR/magenta.bin

# if you have an extra bootfs image (see above):
$BUILDDIR/tools/bootserver $BUILDDIR/magenta.bin /path/to/extra.bootfs
```

By default bootserver will continue to run and every time it obsveres a netboot
beacon it will send the kernel (and bootfs if provided) to that device.  If you
pass the -1 option, bootserver will exit after a successful boot instead.


### Via Magentaboot
Magentaboot is a mechanism that allows a magenta system to serve as the
bootloader for magenta itself. Magentaboot speaks the same boot protocol as
Gigaboot described above.

To use magentaboot, pass the `netsvc.netboot=true` argument to magenta via the
kernel command line. When magentaboot starts, it will attempt to fetch and boot
into a magenta system from a bootserver running on the attached host.

## Network Log Viewing

The default build of Magenta includes a network log service that multicasts the
system log over the link local IPv6 UDP.  Please note that this is a quick hack
and the protocol will certainly change at some point.

For now, if you're running Magenta on QEMU with the -N flag or running on hardware
with a supported ethernet interface (ASIX USB Dongle or Intel Ethernet on NUC),
the loglistener tool will observe logs broadcast over the local link:

```
$BUILDDIR/tools/loglistener
```

## Debugging

For random tips on debugging in the magenta environment see
[debugging](debugging/tips.md).

## Contribute changes
* See [contributing.md](contributing.md).
