Fuchsia
============

Pink + Purple == Fuchsia (a new Operating System)

Welcome to Fuchsia! This is a top-level entry point for the project. From here
we try to link to everything you need to get started, use, and develop for
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

* Ensure `goma` is installed on your machine for faster builds.

## Build Fuchsia

### Get the source
Follow all steps in this document and then return to this document:
  * [Fuchsia Source](getting_source.md)

### Setup Build Environment

Source the
[`env.sh`](https://fuchsia.googlesource.com/scripts/+/master/env.sh)
script, which provides helpful shell functions for Fuchsia
development. The following command also changes the command prompt and
sets up for a x86-64 build.

(If you don't want to change your command prompt, omit `envprompt`.)

```
source scripts/env.sh && envprompt && fset x86-64
```

Alternatively, you may use the [underlying build scripts](build_system.md).

Run `envhelp` to see other useful shell functions, and `envhelp <function>` for
specific usage information.

Optionally, you might find it useful to add a shell function `fuchsia` as a
shortcut to setup the build environment. For that, add this to your shell startup
script (e.g. `~/.bashrc`):

```
export FUCHSIA_DIR=/path/to/my/fuchsia/source
function fuchsia() {
  source $FUCHSIA_DIR/scripts/env.sh && envprompt && fgo && fset x86-64 "$@"
}
```

#### [optional] Customize Build Environment

By default you will get a x86-64 debug build. You can skip this section unless
you want something else.

`ccache` accelerates builds by caching artifacts from previous builds.
`ccache` will be enabled automatically by default if the `CCACHE_DIR`
environment variable is set and refers to a directory that exists.
To disable `ccache`, specify `--no-ccache`.

[Googlers only: `goma` accelerates builds by distributing compilation
across many machines.  If you have `goma` installed in `~/goma`, it will used
by default in preference to `ccache`.  To disable `goma`, specify `--no-goma`.]

Run `fset-usage` to see a list of build options. Some examples:

```
fset x86-64              # x86-64 debug build
fset arm64               # arm64 debug build
fset x86-64 --release    # x86-64 release build
fset x86-64 --ccache     # x86-64 debug build, force use of ccache even if goma is available
fset x86-64 --no-goma    # x86-64 debug build, disable use of goma
fset x86-64 --no-ccache  # x86-64 debug build, disable use of ccache
```

### Start the build

Once you have setup your build environment, simply run:

```
fbuild
```

This builds Zircon, the sysroot, and the default Fuchsia build.

## Boot Fuchsia

### Boot from hardware

There are three options for booting Fuchsia on hardware: network booting (see
below), booting from USB (see below), or [installing](https://fuchsia.googlesource.com/install-fuchsia/+/master/README.md)
Fuchsia on internal storage. In all cases you'll need to put some code on the
target hardware, using a USB drive is a good option for doing this.

If you want to netboot or create a bootable USB drive, but not install Fuchsia
you can use the [build-bootable-usb-gigaboot.sh script](https://fuchsia.googlesource.com/scripts/+/master/build-bootable-usb-gigaboot.sh).
If you plan to netboot, pass the `-m` and `-f` options to skip copying over the
Zircon kernel and Fuchsia system images since the bootserver will supply these.

It may be useful to look at some of the hardware specific instructions. The
Raspberry Pi 3 requires very different procedures and the other guides may help
with hardware-specific firmware configuration.

* [Acer Switch Alpha 12](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md)
* [Intel NUC](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/nuc.md)
* [Raspberry Pi 3](https://fuchsia.googlesource.com/zircon/+/master/docs/targets/rpi3.md)

Once your hardware is configured, you can run `fboot` to start the bootserver.

### Boot from QEMU

If you don't have the supported hardware, you can run Fuchsia under emulation
using [QEMU](https://fuchsia.googlesource.com/zircon/+/HEAD/docs/qemu.md).
Fuchsia includes prebuilt binaries for QEMU under `buildtools/qemu`.

The `frun` command will launch Zircon within QEMU, using the locally built
`user.bootfs`:

```
frun
```

There are various flags for `frun` to control QEMU's configuration:
* `-m` sets QEMU's memory size in MB.
* `-g` enables graphics (see below).
* `-N` enables networking (see below).

Use `frun -h` to see all available options.

#### Enabling Graphics

To enable graphics under QEMU, add the `-g` flag to `frun`:

```
frun -g
```

#### Enabling Network

Note: Networking support within QEMU is only available under x86_64.

First, [configure](https://fuchsia.googlesource.com/zircon/+/master/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only)
a virtual interface for QEMU's use.

Once this is done you can add the `-N` and `-u` flags to `frun`:

```
frun -N -u $FUCHSIA_SCRIPTS_DIR/start-dhcp-server.sh
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

(qemu users must enable graphics with -g.)

Most graphical applications in Fuchsia use the
[mozart](https://fuchsia.googlesource.com/mozart) system compositor. You can launch
such applications, commonly found in `/system/apps`, like this:

```
launch spinning_square_view
```

Source code for mozart example apps is
[here](https://fuchsia.googlesource.com/mozart/+/HEAD/examples/).

When you launch something that uses mozart, does hardware accelerated graphics, or if you build the
the [default](https://fuchsia.googlesource.com/packages/+/master/gn/default) package (which will
boot into the Fuchsia system UI), Fuchsia will enter "graphics mode", which will not display any
of the text shells. In order to use the text shell, you will need to enter "console mode" by
pressing Alt-Escape. In console mode, Alt-Tab will have the behavior described in the previous
section, and pressing Alt-Escape again will take you back to the graphical shell.

If you would like to use a text shell inside a terminal emulator from within the graphical shell
you can launch [moterm](https://fuchsia.googlesource.com/moterm/) by selecting the "Ask Anything"
box and typing "moterm"

## Contribute changes
* See [CONTRIBUTING.md](CONTRIBUTING.md).

## Additional helpful documents

* Using Zircon - copying files, network booting, log viewing, and more are [here](https://fuchsia.googlesource.com/zircon/+/master/docs/getting_started.md#Copying-files-to-and-from-Zircon)
* [Fuchsia documentation](https://fuchsia.googlesource.com/docs) hub
* Build [Fuchsia's toolchain](toolchain.md)
* More about the [build commands](build_system.md) called under-the-hood by `fbuild`
* More information on the system bootstrap application is
[here](https://fuchsia.googlesource.com/application/+/HEAD/src/bootstrap/).
