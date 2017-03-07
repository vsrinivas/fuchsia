Fuchsia
============

Pink + Purple == Fuchsia (a new Operating System)

Welcome to Fuchsia! This is a top-level entry point for the project. From here
we try to link to everything you need to get started, use, and develop for
Fuchsia.

## Prerequisites

### Prepare your build environment

The Fuchsia source
includes [Magenta](https://fuchsia.googlesource.com/magenta/+/HEAD/README.md),
the core platform that underpins Fuchsia. Click the link below, follow the
steps under *Preparing the build environment*, and then return to this document.
(Do not continue to the *Install Toolchains* section.)

* [Preparing the Magenta build environment](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md#Preparing-the-build-environment).

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

```
source scripts/env.sh && envprompt && fset x86-64
```

Alternatively, you may [use the underlying build scripts](build_system.md).

Run `envhelp` to see other useful shell functions, and `envhelp <function>` for
specific usage information.

Optionally, you might find it useful to add a shell function `fuchsia` as a
shortcut to setup the build environment. For that, add this to your shell startup
script (e.g. `~/.bashrc`):

```
export FUCHSIA_ROOT=/path/to/my/fuchsia/source
function fuchsia() {
  source $FUCHSIA_ROOT/scripts/env.sh && envprompt && fgo && fset x86-64 "$@"
}
```

#### [optional] Customize Build Environment

By default you will get a x86-64 debug build. You can skip this section unless
you want something else.

[Googlers only: If you have `goma` installed, it will also be used by default.
Prefer `goma` over `ccache`. Note: to use `ccache` or `goma` you must install
them first.]

Run `fset-usage` to see a list of build options. Some examples:

```
fset x86-64           # x86-64 debug build, no goma, no ccache
fset arm64            # arm64 debug build, no goma, no ccache
fset x86-64 --release # x86-64 release build, no goma, no ccache
fset x86-64 --ccache  # x86-64 debug build, ccache enabled
```

### Start the build

Once you have setup your build environment, simply run:

```
fbuild
```

This builds Magenta, the sysroot, and the default Fuchsia build.

After Fuchsia is built, you will have a Magenta (`magenta.bin`) image and a
`user.bootfs` file in `out/debug-{arch}/`.

## Boot Fuchsia

### Boot from hardware

If you have the appropriate hardware, you can run Fuchsia on any of these
devices. Follow the links for configuration information.

* [Acer Switch Alpha 12](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/acer12.md)
* [Intel NUC](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/nuc.md)
* [Raspberry Pi 3](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/rpi3.md)

Once you've configured the hardware, run `fboot` on your host system, then boot
the hardware from the USB thumb drive that can be built following the directions
at the end of the Acer Switch page.

### Boot from QEMU

If you don't have the supported hardware, you can run Fuchsia under emulation
using [QEMU](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/qemu.md).
Fuchsia includes prebuilt binaries for QEMU under `buildtools/qemu`.

The `frun` command will launch Magenta within QEMU, using the locally built
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

First, [configure](https://fuchsia.googlesource.com/magenta/+/master/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only)
a virtual interface for QEMU's use.

Once this is done you can add the -N and -u flags to `frun`:

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
- Tabs 1, 2 and 3 contain Fuchsia shells.
- Tab 4 contains a Magenta shell.
- Tabs 5 and higher contain applications you've launched.

Fuchsia has two shells. The Magenta shell talks directly to Magenta and has the
prompt "magenta$". The Fuchsia shell supports additional features like the
Application Manager. The prompt for the Fuchsia shell is just "$". You should
use the Fuchsia shell for the examples below.

### Launch a graphical application

(qemu users must enable graphics with -g.)

Most graphical applications in Fuchsia use the
[mozart](https://fuchsia.googlesource.com/mozart) engine. You can launch
such applications, commonly found in `/system/apps`, like this:

```
launch spinning_square_view
```

Note: Fuchsia is currently doing software-based rendering and not using the GPU.

Source code for mozart example apps is
[here](https://fuchsia.googlesource.com/mozart/+/HEAD/examples/).

## Contribute changes
* See [CONTRIBUTING.md](CONTRIBUTING.md).

## Additional helpful documents

* Using Magenta - copying files, network booting, log viewing, and more are [here]
(https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md#Copying-files-to-and-from-Magenta)
* [Fuchsia documentation](https://fuchsia.googlesource.com/docs) hub
* Build [Fuchsia's toolchain](building_toolchain.md)
* More about the [build commands](build_system.md) called under-the-hood by `fbuild`
* More information on the system bootstrap application is
[here](https://fuchsia.googlesource.com/modular/+/HEAD/src/bootstrap/).
