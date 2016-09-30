# Quick Start Recipes

## Checking out the source code

The Magenta Git repository is located
at: https://fuchsia.googlesource.com/magenta

To clone the repository, assuming you setup the $SRC variable
in your environment:
```shell
$ git clone https://fuchsia.googlesource.com/magenta $SRC/magenta
```

For the purpose of this document, we will assume that Magenta is checked
out in $SRC/magenta and that we will build toolchains, qemu, etc alongside
that.  Various make invocations are presented with a "-j32" option for
parallel make.  If that's excessive for the machine you're building on,
try -j16 or -j8.

## Preparing the build environment

On Ubuntu this should obtain the necessary pre-reqs:
```
sudo apt-get install texinfo libglib2.0-dev autoconf libtool libsdl-dev build-essential
```

On Mac with homebrew:
```
brew install wget pkg-config glib autoconf automake libtool
```

On Mac with MacPorts:
```
port install autoconf automake libtool libpixman pkgconfig glib2
```

## Install Toolchains

If you're developing on Linux or OSX, there are prebuilt toolchain binaries avaiable.
Just run this script from your Magenta working directory:

```
./scripts/download-toolchain
```

## Build Toolchains (Optional)

If the prebuilt toolchain binaries do not work for you, there are a
set of scripts which will download and build suitable gcc toolchains
for building Magenta for ARM32, ARM64, and x86-64 architectures:

```
cd $SRC
git clone https://fuchsia.googlesource.com/third_party/gcc_none_toolchains toolchains
cd toolchains
./doit -a 'arm aarch64 x86_64' -f -j32
```

## Build Qemu

You can skip this if you're only testing on actual hardware, but the emulator
is handy for quick local tests and generally worth having around.

If you don't want to install in /usr/local (the default), which will require you
to be root, add --prefix=/path/to/install  (perhaps $HOME/qemu) and then you'll
need to add /path/to/install/bin to your PATH.

```
cd $SRC
git clone https://fuchsia.googlesource.com/third_party/qemu
cd qemu
./configure --target-list=arm-softmmu,aarch64-softmmu,x86_64-softmmu
make -j32
sudo make install
```

## Configure PATH

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

## Run Magenta under Qemu

```
# for aarch64
./scripts/run-magenta-arm64

# for x86-64
./scripts/run-magenta-x86-64
```

The -h flag will list a number of options, including things like -b to rebuild first
if necessary and -g to run with a graphical framebuffer.

To exit qemu, enter Ctrl-a x. Use Ctrl-a h to see other commands.

## Building Magenta for all targets

```
# The -r enables release builds as well
./scripts/buildall -r
```

Please build for all targets before submitting to ensure builds work
on all architectures.

## Enabling Networking under Qemu (x86-64 only)

The run-magenta-x86-64 script, when given the -N argument will attempt to create
a network interface using the Linux tun/tap network device named "qemu".  Qemu
does not need to be run with any special privileges for this, but you need to
create a persistent tun/tap device ahead of time (which does require you be root):

On Linux:

```
sudo apt-get install uml-utilities
sudo tunctl -u $USER -t qemu
sudo ifconfig qemu up
```

This is sufficient to enable link local IPv6 (as the loglistener tool uses).

On macOS:

macOS does not support tun/tap devices out of the box, however there is a widely
used set of kernel extensions called tuntaposx which can be downloaded
[here](http://tuntaposx.sourceforge.net/download.xhtml). Once the installer
completes, the extensions will create up to 16 tun/tap devices. The
run-magenta-x86-64 script uses /dev/tap0.

```
sudo chown $USER /dev/tap0

# Run magenta in QEMU, which will open /dev/tap0
./scripts/run-magenta-x86-64 -N

# (In a different window) bring up tap0 with a link local IPv6 address
sudo ifconfig tap0 inet6 fc00::/7 up
```

**NOTE**: One caveat with tuntaposx is that the network interface will
automatically go down when QEMU exits and closes the network device. So the
network interface needs to be brought back up each time QEMU is restarted.

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
is capable of including a second bootfs image which is provided by Qemu or the
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

For Qemu, use the -x option to specify an extra bootfs image.

## Network Booting

The GigaBoot20x6 bootloader speaks a simple network boot protocol (over IPV6 UDP)
which does not require any special host configuration or privileged access to use.

It does this by taking advantage of IPV6 Link Local Addressing and Multicast,
allowing the device being booted to advertise its bootability and the host to find
it and send a system image to it.

If you have a device (for example a Broadwell or Skylake Intel NUC) running
GigaBoot20x6, you can boot Magenta on it like so:

```
$BUILDDIR/tools/bootserver $BUILDDIR/magenta.bin

# if you have an extra bootfs image (see above):
$BUILDDIR/tools/bootserver $BUILDDIR/magenta.bin /path/to/extra.bootfs
```

By default bootserver will continue to run and every time it obsveres a netboot
beacon it will send the kernel (and bootfs if provided) to that device.  If you
pass the -1 option, bootserver will exit after a successful boot instead.

## Network Log Viewing

The default build of Magenta includes a network log service that multicasts the
system log over the link local IPv6 UDP.  Please note that this is a quick hack
and the protocol will certainly change at some point.

For now, if you're running Magenta on Qemu with the -N flag or running on hardware
with a supported ethernet interface (ASIX USB Dongle or Intel Ethernet on NUC),
the loglistener tool will observe logs broadcast over the local link:

```
$BUILDDIR/tools/loglistener
```
