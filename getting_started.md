Fuchsia
============

Pink + Purple == Fuchsia (a new Operating System)

Welcome to Fuchsia! This is a top-level entry point for the project. From here
we try to link to everything you need to get started, use, and develop for
Fuchsia.

## Getting the source
Get the Fuchsia source by following these two steps and then return to this document:
  * [Install prerequisites](https://fuchsia.googlesource.com/docs/+/HEAD/getting_source.md#Prerequisites) for Jiri, a tool for multi-repo development.
  * [Create a new checkout](https://fuchsia.googlesource.com/docs/+/HEAD/getting_source.md#Creating-a-new-checkout) of Fuchsia.

## Prerequisites

### Magenta Prerequisites

The Fuchsia source
includes [Magenta](https://fuchsia.googlesource.com/magenta/+/HEAD/README.md),
the core platform which underpins Fuchsia. Follow this step to install the
Magenta build prerequisites and then return to this document. (You can ignore
the toolchain installation instructions unless you want to build your own; the
Fuchsia manifest will automatically obtain a prebuilt toolchain.)

* [Preparing the Magenta build environment](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md#Preparing-the-build-environment).

### [Googlers only] Goma

Ensure `goma` is installed on your machine for faster builds.

## Build Fuchsia

### Setup Build Environment

Source the [`env.sh`](scripts/env.sh) script, which provides helpful shell
functions for Fuchsia development. The following command also changes the
command prompt and sets up for a x86-64 build.

```
source scripts/env.sh && envprompt && fset x86-64
```

Alternatively, you may [use the underlying build scripts](build_system.md).

Run `envhelp` to see other useful shell functions, and `envhelp <function>` for
specific usage information.

Optionally, you might find it useful to add a shell function `fuchsia` as a
shortcut to setup the build environment. For that, add this your shell startup
script (e.g. `~/.bashrc`):

```
export FUCHSIA_ROOT=/path/to/my/fuchsia/source
function fuchsia() {
  source $FUCHSIA_ROOT/scripts/env.sh && envprompt && fgo && fset x86-64 "$@"
}
```

### [optional] Customize Build Environment

By default you will get a x86-64 debug build, and you can skip this step unless
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

### Build Fuchsia

Once you have setup your build environment, simply run:

```
fbuild
```

This builds Magenta, the sysroot, and the default Fuchsia build.

After Fuchsia is built, you will have a Magenta (`magenta.bin`) image and a
`user.bootfs` file in `out/debug-{arch}/`.

### Run Fuchsia in QEMU

You can run Fuchsia under emulation
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

When Fuchsia has booted and started an MXCONSOLE, you can run programs!

For example, to receive deep wisdom, run:

```
fortune
```

#### Working with Fuchsia system components

The `bootstrap` utility sets up the environment and system services required to
use and develop Fuchsia applications. You can run individual Fuchsia
applications invoking their url:

    @ bootstrap [args...] <app url> <app args...>

Bootstrap can also be run without any initial apps for debugging purposes.

    @ bootstrap -

You can run additional Fuchsia applications within an existing bootstrap
environment by specifying the `boot` scope:

    @boot <app url> <app args...>

More information on bootstrap and its uses can be
found [here](https://fuchsia.googlesource.com/modular/+/HEAD/src/bootstrap/).

#### Enabling Graphics

Note: Graphics mode can only be run in a GUI with a terminal.

To enable graphics, add the `-g` flag to `frun`:

```
frun -g
```

Run graphical applications
using [mozart](https://fuchsia.googlesource.com/mozart) in `/system/apps` like
this:

```
@ bootstrap launch spinning_square_view
```

Some more mozart example apps are [here](https://fuchsia.googlesource.com/mozart/+/HEAD/examples/).

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

### Running Fuchsia on hardware

* [Acer Switch Alpha 12](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/acer12.md)
* [Intel NUC](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/nuc.md)
* [Raspberry Pi 3](https://fuchsia.googlesource.com/magenta/+/master/docs/targets/rpi3.md)

## Contributing changes
* See [CONTRIBUTING.md](https://fuchsia.googlesource.com/docs/+/HEAD/CONTRIBUTING.md).

## Additional helpful documents

* [Fuchsia documentation](https://fuchsia.googlesource.com/docs) hub.
* Building [Fuchsia's toolchain](https://fuchsia.googlesource.com/docs/+/HEAD/building_toolchain.md)
* More about the [build commands](build_system.md) called under-the-hood by `fbuild`.
