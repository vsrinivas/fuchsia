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

Note: A quick overview of the basic build-and-pave workflow can be found [here](development/workflows/build_and_pave_quickstart.md).


If you added `.jiri_root/bin` to your path as part of getting the source code,
the `fx` command should already be in your path. If not, the command is also
available as `scripts/fx`.

```sh
fx set core.x64 --with //bundles:kitchen_sink
fx build
```

The first command selects the build configuration you wish to build and
generates the build system itself in an output directory (e.g., `out/x64`).
Fuchsia can ephemerally download [packages](development/build/boards_and_products.md) over the network;
here we use the `--with` flag to include the bundle named `kitchen_sink` which is an idiom in
english meaning "practically everything". As you become more focused in your development, you will
probably use different `fx set` options to minimize build times.

The second command, `fx build` actually executes the build, transforming the source code in
build products. If you modify the source tree, you can do an incremental build
by re-running the `fx build` command alone. `fx -i build` starts a watcher
and automatically builds whenever a file is changed.

See the [underlying build system](development/build/README.md) for more details.

### _Optional:_ Customize Build Environment

By default you will get a x64 debug build. You can skip this section unless
you want something else.

Run `fx set` to see a list of build options. Some examples:

```sh
fx set workstation.x64     # x64 debug build
fx set core.arm64          # arm64 debug build
fx set core.x64 --release  # x64 release build
```

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
[instructions](development/workflows/paving.md) will help you get up and running
with.

Note: A quick overview of the basic build-and-pave workflow can be found
[here](development/workflows/build_and_pave_quickstart.md).

### Boot from QEMU

If you don't have the supported hardware, you can run Fuchsia under emulation
using [QEMU](/docs/development/emulator/qemu.md).
Fuchsia includes prebuilt binaries for QEMU under `prebuilt/third_party/qemu`.

The `fx run` command will launch Zircon within QEMU, using the locally built
disk image:

```sh
fx run
```

There are various flags for `fx run` to control QEMU's configuration:

* `-m` sets QEMU's memory size in MB.
* `-g` enables graphics (see below).
* `-N` enables networking (see below).
* `-k` enables KVM acceleration on Linux.

Use `fx run -h` to see all available options.

Note: Before you can run any commands, you will need to follow the instructions in the [Explore Fuchsia](#explore-fuchsia) section below.

#### QEMU tips

* `ctrl+a x` will exit QEMU in text mode.
* `ctrl+a ?` or `ctrl+a h` prints all supported commands.

#### Enabling Graphics

Note: Graphics under QEMU are extremely limited due to a lack of Vulkan
support. Only the Zircon UI renders.

To enable graphics under QEMU, add the `-g` flag to `fx run`:

```sh
fx run -g
```

#### Enabling Network

First, [configure](/docs/development/emulator/qemu.md#enabling_networking_under_qemu) a
virtual interface for QEMU's use.

Once this is done you can add the `-N` and `-u` flags to `fx run`:

```sh
fx run -N -u scripts/start-dhcp-server.sh
```

The `-u` flag runs a script that sets up a local DHCP server and NAT to
configure the IPv4 interface and routing.

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
dm help
dm shutdown
```

### Change some source

Almost everything that exists on a Fuchsia system is stored in a Fuchsia
package. A typical development
[workflow](development/workflows/package_update.md) involves re-building and
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

Most graphical components in Fuchsia use the [Scenic](/garnet/bin/ui/) system
compositor. You can launch such components, commonly found in `/system/apps`,
like this:

```sh
present_view fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx
```

Source code for Scenic example apps is [here](/garnet/examples/ui).

When you launch something that uses Scenic, uses hardware-accelerated graphics, or if you build
the [default](https://fuchsia.googlesource.com/topaz/+/master/packages) package (which will
boot into the Fuchsia System UI), Fuchsia will enter "graphics mode", which will not display any
of the text shells. In order to use the text shell, you will need to enter "console mode" by
pressing Alt-Escape. In console mode, Alt-Tab will have the behavior described in the previous
section, and pressing Alt-Escape again will take you back to the graphical shell.

If you would like to use a text shell inside a terminal emulator from within the graphical shell
you can launch the [term](https://fuchsia.googlesource.com/topaz/+/master/app/term) by selecting the
"Ask Anything" box and typing `moterm`.

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
* [Workflow tips and FAQ](development/workflows/workflow_tips_and_faq.md) that help increase
  productivity.
