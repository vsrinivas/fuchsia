# Fuchsia

Pink + Purple == Fuchsia (a new Operating System)

Welcome to Fuchsia! This document has everything you need to get started with
Fuchsia.

Note: The Fuchsia source includes [Zircon](/zircon/README.md),
the core platform that underpins Fuchsia. The Fuchsia build process will
build Zircon as a side-effect; to work on Zircon only, read and follow
Zircon's [Getting Started](/docs/development/kernel/getting_started.md) doc.

[TOC]

## Prerequisites

### Prepare your build environment (once per build environment)

#### Debian

```
sudo apt-get install build-essential curl git python unzip
```

#### macOS

1.  Install Command Line Tools:

    ```
    xcode-select --install
    ```

1.  In addition to Command Line Tools, you also need to
    install a recent version of [Xcode](https://developer.apple.com/xcode/).

## Get the Source

Follow [the instructions to get the Fuchsia source](development/source_code/README.md)
and then return to this document.

## Build Fuchsia

Note: A quick overview of the basic build-and-pave workflow can be found [here](development/build/build_and_pave_quickstart.md).


If you added `.jiri_root/bin` to your path as part of getting the source code,
the `fx` command should already be in your path. If not, the command is also
available as `scripts/fx`.

```sh
fx set core.x64 --with //bundles:kitchen_sink
fx build
```

The `fx set` command configures the contents of your build and generates
build rules and metadata in the default output directories, `out/default` and
`out/default.zircon`. The argument `core.x64` refers to
[product and board definitions](development/build/boards_and_products.md) that
describe, among other things, what packages are built and available
to your Fuchsia device.

A Fuchsia device can ephemerally download and install packages over the network,
and in a development environment, your development workstation is the source of
these ephemeral packages. The board and product definitions contain a set of packages,
but if you need to add other packages, use the `--with` flag. This example
includes `kitchen_sink`, which is an idiom in english meaning "practically
everything". As you become more focused in your development, you will probably
use more specific `--with` options to minimize build times.

The `fx build` command executes the build, transforming source code into
packages and other build artifacts. If you modify source code,
you can do an incremental build by re-running the `fx build` command alone.
`fx -i build` starts a watcher and automatically builds whenever a file is changed.

See the [underlying build system](development/build/README.md) for more details.

{% dynamic if user.is_googler %}
### Accelerate the build with goma

`goma` accelerates builds by distributing compilation across
many machines.  If you have `goma` installed in `~/goma`, it is used by default.

If goma cannot be found, `ccache` is used if available.

It is also used by default in preference to `ccache`.

To disable using goma, pass `--no-goma` to `fx set`.

{% dynamic endif %}

### _Optional:_ Accelerate the build with ccache
[`ccache`](https://ccache.dev/){: .external} accelerates builds by caching artifacts
from previous builds. `ccache` is enabled automatically if the `CCACHE_DIR` environment
variable is set and refers to a directory that exists.

To override the default behaviors, pass flags to `fx set`:

```sh
--ccache     # force use of ccache even if goma is available
--no-ccache  # disable use of ccache
```

## Boot Fuchsia

### Installing and booting from hardware

To get Fuchsia running on hardware requires using the paver, which these
[instructions](development/hardware/paving.md) will help you get up and running
with.

Note: A quick overview of the basic build-and-pave workflow can be found
[here](development/build/build_and_pave_quickstart.md).

### Boot from QEMU

If you don't have the supported hardware, you can run Fuchsia under emulation
using [QEMU](/docs/development/emulator/qemu.md).
Fuchsia includes prebuilt binaries for QEMU under `prebuilt/third_party/qemu`.

The `fx emu` command will launch Fuchsia within QEMU, using the locally built
disk image:

```sh
fx emu
```

There are various flags for `fx emu` to control the emulator configuration:

* `-N` enables networking (see below).
* `--headless` disable graphics (see below).
* `-c` pass additional arguments to the kernel.

Use `fx emu -h` to see all available options.

Note: Before you can run any commands, you will need to follow the instructions in the [Explore Fuchsia](#explore-fuchsia) section below.

#### Enabling Network

In order for ephemeral software to work in the emulator, an IPv6 network must
be configured.

On macOS: Install "http://tuntaposx.sourceforge.net/download.xhtml"
On Linux: Run `sudo ip tuntap add dev qemu mode tap user $USER && sudo ip link set qemu up`

Now the emulator can be run with networking enabled:

```
fx emu -N
```

The above is sufficient for ephemeral software (that is served by `fx serve`,
see below) to work, including many tools such as `uname` and `fortune` (if
built).

Users who also wish to reach the internet from the emulator will need to
configure some manner of IP forwarding and IPv4 support on the emulator TAP
interface. Details of this process are beyond the scope of this document.

## Explore Fuchsia {#explore-fuchsia}

In a separate shell, start the development update server, if it isn't already
running:

```sh
fx serve
```

Boot Fuchsia with networking. This can be done either in QEMU via the `-N` flag,
or on a paved hardware, both described above.
When Fuchsia has booted and displays the "$" shell prompt, you can run programs!

For example, to receive deep wisdom, run:

```sh
fortune
```

To shutdown or reboot Fuchsia, use the `dm` command:

```sh
dm shutdown
dm reboot
```

### Change some source

Almost everything that exists on a Fuchsia system is stored in a Fuchsia
package. A typical development
[workflow](development/build/package_update.md) involves re-building and
pushing Fuchsia packages to a development device or QEMU virtual device.

Make a change to the rolldice binary in `examples/rolldice/src/main.rs`.

Re-build and push the rolldice package to a running Fuchsia device with:

```sh
fx build-push rolldice
```

From a shell prompt on the Fuchsia device, run the updated rolldice component
with:

```sh
rolldice
```

### Select a tab

Fuchsia shows multiple tabs after booting [with graphics
enabled](#enabling-graphics). The currently selected tab is highlighted in
yellow at the top of the screen.

The following keyboard shortcuts help you navigate the terminal:

- Alt+Tab switches between tabs.
- Alt+F{1,2,...} switches directly to a tab.
  - Tab zero is the console, which displays the boot and component log.
  - Tabs 1, 2 and 3 contain shells.
  - Tabs 4 and higher contain components you've launched.
- Alt+Up/Down scrolls up and down by lines.
- Shift+PgUp/PgDown scrolls up and down by half page.
- Ctrl+Alt+Delete reboots.

Note: To select tabs, you may need to enter "console mode". See the next section for details.

### Launch a graphical component

Warning: QEMU does not support Vulkan and therefore cannot run our graphics stack. Commands in this section will not work on QEMU.

Most graphical components in Fuchsia use the [Scenic](/src/ui/scenic/) system
compositor. You can launch such components, commonly found in `/system/apps`,
like this:

```sh
present_view fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx
```

Source code for Scenic example apps is [here](/src/ui/examples).

When you launch something that uses Scenic, uses hardware-accelerated graphics, or if you build
the [default](https://fuchsia.googlesource.com/topaz/+/master/packages) package (which will
boot into the Fuchsia System UI), Fuchsia will enter "graphics mode", which will not display any
of the text shells. In order to use the text shell, you will need to enter "console mode" by
pressing Alt-Escape. In console mode, Alt-Tab will have the behavior described in the previous
section, and pressing Alt-Escape again will take you back to the graphical shell.

## Running tests

Compiled test binaries are cached in pkgfs like other components, and are referenced by a URI.
You can run a test by invoking it in the terminal. For example:

```sh
run fuchsia-pkg://fuchsia.com/ledger_tests#meta/ledger_unittests.cmx
```

If you want to leave Fuchsia running and recompile and re-run a test, run
Fuchsia with networking enabled in one terminal, then in another terminal, run:

```sh
fx run-test <test name> [<test args>]
```

You may wish to peruse the [testing FAQ](development/testing/faq.md).

## Contribute changes

* See [CONTRIBUTING.md](/CONTRIBUTING.md).

## Additional helpful documents

* [Fuchsia documentation](README.md) hub
* Working with Zircon - [copying files, network booting, log viewing, and
more](/docs/development/kernel/getting_started.md#Copying-files-to-and-from-Zircon)
* [Documentation Standards](/docs/contribute/best-practices/documentation_standards.md) - best practices
  for documentation
* [Information on the system bootstrap component](/src/sys/sysmgr/).
* [Workflow tips and FAQ](development/source_code/workflow_tips_and_faq.md) that help increase
  productivity.
