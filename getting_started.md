# Fuchsia

Pink + Purple == Fuchsia (a new Operating System)

Welcome to Fuchsia! This document has everything you need to get started with
Fuchsia.

*** note
NOTE: The Fuchsia source includes
[Zircon](https://fuchsia.googlesource.com/zircon/+/master/README.md),
the core platform that underpins Fuchsia.
The Fuchsia build process will build Zircon as a side-effect;
to work on Zircon only, read and follow Zircon's
[Getting Started](https://fuchsia.googlesource.com/zircon/+/master/docs/getting_started.md)
doc.
***

## Prerequisites

### Prepare your build environment (Once per build environment)

### Ubuntu

```
sudo apt-get install texinfo libglib2.0-dev autoconf libtool libsdl-dev build-essential golang git build-essential curl unzip
```

### macOS

Install the Xcode Command Line Tools:
```
xcode-select --install
```

Install the other pre-reqs:
* Using Homebrew:
```
# Install Homebrew
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
# Install packages
brew install wget pkg-config glib autoconf automake libtool golang
```

* Using MacPorts:
```
# Install MacPorts
# See https://guide.macports.org/chunked/installing.macports.html
port install autoconf automake libtool libpixman pkgconfig glib2
```

### [Googlers only] Goma

Ensure `goma` is installed on your machine for faster builds.

## Build Fuchsia

### Get the source

[Get the Fuchsia source](getting_source.md) and then return to this document.

### Build

If you added `.jiri_root/bin` to your path as part of getting the source code,
the `fx` command should already be in your path. If not, the command is also
available as `scripts/fx`.

```
fx set x86-64
fx full-build
```

The first command selects the build configuration you wish to build and
generates the build system itself in an output directory
(e.g., `out/debug-x86-64`).

The second command actually executes the build, transforming the source code in
build products. If you modify the source tree, you can do an incremental build
by re-running the `fx full-build` command alone.

Alternatively, you can use the [underlying build system directly](build_system.md).

#### [optional] Customize Build Environment

By default you will get a x86-64 debug build. You can skip this section unless
you want something else.

Run `fset-usage` to see a list of build options. Some examples:

```
fx set x86-64              # x86-64 debug build
fx set arm64               # arm64 debug build
fx set x86-64 --release    # x86-64 release build
```

#### [optional] Accelerate builds with `ccache` and `goma`

`ccache` accelerates builds by caching artifacts from previous builds. `ccache`
is enabled automatically if the `CCACHE_DIR` environment variable is set and
refers to a directory that exists.

[Googlers only: `goma` accelerates builds by distributing compilation across
many machines.  If you have `goma` installed in `~/goma`, it is used by default.
It is also used by default in preference to `ccache`.]

To override the default behaviors, pass flags to `fx set`:

```
--ccache     # force use of ccache even if goma is available
--no-ccache  # disable use of ccache
--no-goma    # disable use of goma
```

## Boot Fuchsia

### Boot from hardware

There are three options for booting Fuchsia on hardware: network booting (see
below), booting from USB (see below), or [installing](https://fuchsia.googlesource.com/install-fuchsia/+/master/README.md)
Fuchsia on internal storage. In all cases you'll need to put some code on the
target hardware, using a USB drive is a good option for doing this.

If you want to netboot or create a bootable USB drive, but not install Fuchsia,
you can use the [build-bootable-usb-gigaboot.sh script](https://fuchsia.googlesource.com/scripts/+/master/build-bootable-usb-gigaboot.sh).
If you plan to netboot, pass the `-m` and `-f` options to skip copying over the
Zircon kernel and Fuchsia system images since the bootserver will supply these.

* [Acer Switch Alpha 12](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md)
* [Intel NUC](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/nuc.md)

Once your hardware is configured, you can run `fx boot` to start the bootserver.

### Boot from QEMU

If you don't have the supported hardware, you can run Fuchsia under emulation
using [QEMU](https://fuchsia.googlesource.com/zircon/+/HEAD/docs/qemu.md).
Fuchsia includes prebuilt binaries for QEMU under `buildtools/qemu`.

The `fx run` command will launch Zircon within QEMU, using the locally built
`user.bootfs`:

```
fx run
```

There are various flags for `fx run` to control QEMU's configuration:
* `-m` sets QEMU's memory size in MB.
* `-g` enables graphics (see below).
* `-N` enables networking (see below).

Use `fx run -h` to see all available options.

#### Enabling Graphics

To enable graphics under QEMU, add the `-g` flag to `fx run`:

```
fx run -g
```

#### Enabling Network

Note: Networking support within QEMU is only available under x86_64.

First, [configure](https://fuchsia.googlesource.com/zircon/+/master/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only)
a virtual interface for QEMU's use.

Once this is done you can add the `-N` and `-u` flags to `fx run`:

```
fx run -N -u $FUCHSIA_SCRIPTS_DIR/start-dhcp-server.sh
```

The `-u` flag runs a script that sets up a local DHCP server and NAT to
configure the IPv4 interface and routing.

## Explore Fuchsia

When Fuchsia has booted and displays the "$" shell prompt, you can run programs!

For example, to receive deep wisdom, run:

```
fortune
```

### Select a tab

Fuchsia shows multiple tabs after booting. The currently selected tab is
highlighted in yellow at the top of the screen. You can switch to the next
tab using Alt-Tab on the keyboard.

- Tab zero is the console and displays the boot and application log.
- Tabs 1, 2 and 3 contain shells.
- Tabs 4 and higher contain applications you've launched.

Note: to select tabs, you may need to enter "console mode". See the next section for details.

### Launch a graphical application

QEMU does not support Vulkan and therefore cannot run our graphics stack.

Most graphical applications in Fuchsia use the
[Mozart](https://fuchsia.googlesource.com/garnet/+/master/bin/ui/) system compositor. You can launch
such applications, commonly found in `/system/apps`, like this:

```
launch spinning_square_view
```

Source code for Mozart example apps is
[here](https://fuchsia.googlesource.com/garnet/+/master/examples/ui).

When you launch something that uses Mozart, uses hardware-accelerated graphics, or if you build
the [default](https://fuchsia.googlesource.com/packages/+/master/gn/default) package (which will
boot into the Fuchsia System UI), Fuchsia will enter "graphics mode", which will not display any
of the text shells. In order to use the text shell, you will need to enter "console mode" by
pressing Alt-Escape. In console mode, Alt-Tab will have the behavior described in the previous
section, and pressing Alt-Escape again will take you back to the graphical shell.

If you would like to use a text shell inside a terminal emulator from within the graphical shell
you can launch [moterm](https://fuchsia.googlesource.com/moterm/) by selecting the "Ask Anything"
box and typing `moterm`.

## Contribute changes

* See [CONTRIBUTING.md](CONTRIBUTING.md).

## Additional helpful documents

* Using Zircon - copying files, network booting, log viewing, and more are [here](https://fuchsia.googlesource.com/zircon/+/master/docs/getting_started.md#Copying-files-to-and-from-Zircon)
* [Fuchsia documentation](https://fuchsia.googlesource.com/docs) hub
* Build [Fuchsia's toolchain](toolchain.md)
* More about the [build commands](build_system.md) called under-the-hood by `fx full-build`
* More information on the system bootstrap application is
[here](https://fuchsia.googlesource.com/application/+/HEAD/src/bootstrap/).
