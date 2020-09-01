# Fuchsia

Pink + Purple == Fuchsia (a new Operating System)

Welcome to Fuchsia! This guide has everything you need to get started with
Fuchsia.

Note: The Fuchsia source includes [Zircon](/zircon/README.md), the core platform
that underpins Fuchsia. To work on Zircon, see
[Getting started with Zircon](/docs/development/kernel/getting_started.md).

## Get the source code

To download the Fuchsia source code and set up your build environment, follow
the instructions in
[Get Fuchsia source code](/docs/development/source_code/README.md).

## Configure and build Fuchsia {#configure-and-build-fuchsia}

To build Fuchsia, you need to be able to run the `fx` command in your terminal.

Note: If you haven't set up your build environment, see
[Set up environment variables](/docs/development/source_code/README.md#set-up-environment-variables).

### Set build configuration

To set your build configuration, run the following command:

```posix-terminal
fx set core.x64
```

The `fx set` command takes a `PRODUCT.BOARD` argument, which defines the
[product and board](/docs/concepts/build_system/boards_and_products.md)
configuration of your build. This configuration informs the build system what
packages to build for your Fuchsia device. `core` is a product with a minimal
feature set, which includes common network capabilities. `x64` refers to the x64
architecture.

See [Configure a build](/docs/development/build/fx.md#configure-a-build) for
more options.

### Speed up the build {#speed-up-the-build}

To reduce the time it takes to build Fuchsia, you can do any of the following:

*   [Speed up the build with Goma](#speed-up-the-build-with-goma)
*   [Speed up the build with ccache](#speed-up-the-build-with-ccache)

#### Speed up the build with Goma {#speed-up-the-build-with-goma}

[Goma](https://chromium.googlesource.com/infra/goma/server/){:.external} is a
distributed compiler service for open source projects such as Chrome, Android
and Fuchsia. If you have access to Goma, run the following command to enable a
Goma client on your machine:

```posix-terminal
fx goma
```

#### Speed up the build with ccache {#speed-up-the-build-with-ccache}

Note: This step is optional.

If you do not have access to Goma, but want to accelerate the Fuchsia build
locally, use <code>[ccache](https://ccache.dev/){:.external}</code> to cache
artifacts from previous builds.

To use `ccache` on Linux, install the following package:

```posix-terminal
sudo apt-get install ccache
```

For macOS, see
[Using CCache on Mac](https://chromium.googlesource.com/chromium/src.git/+/master/docs/ccache_mac.md){:.external}
for installation instructions.

`ccache` is enabled automatically if your `CCACHE_DIR` environment variable
refers to an existing directory.

To override the default behavior, pass the following flags to `fx set`:

*   Force use of ccache even if other accelerators are available:

    ```posix-terminal
    fx set core.x64 --ccache
    ```

*   Disable use of ccache:

    ```posix-terminal
    fx set core.x64 --no-ccache
    ```

### Build Fuchsia

To build Fuchsia, run the following command:

```posix-terminal
fx build
```

The `fx build` command executes the build to transform source code into packages
and other build artifacts.

If you modify source code, re-run the `fx build` command to perform an
incremental build, or run the `fx -i build` command to start a watcher, which
automatically builds whenever you update source code.

See [Execute a build](/docs/development/build/fx.md#execute-a-build) for more
information.

## Set up a Fuchsia device

To run Fuchsia on a device, install Fuchsia on hardware or use an emulator.

### Install Fuchsia on hardware

To get Fuchsia running on hardware, see
[Install Fuchsia on a device](/docs/development/hardware/paving.md).

### Set up the emulator

If you don't have supported hardware, you can run Fuchsia in an emulator using
[FEMU](/docs/development/run/femu.md).

#### Configure network

For Fuchsia's ephemeral software to work in the emulator, you need to configure
an IPv6 network.

##### Linux

To enable networking in FEMU, run the following commands:

```sh
sudo ip tuntap add dev qemu mode tap user $USER
sudo ip link set qemu up
```

##### macOS

You need to install
[TunTap](http://tuntaposx.sourceforge.net/index.xhtml){:.external}, kernel
extensions that allow macOS to create virtual network interfaces.

For macOS 10.9 (Mavericks) and 10.10 (Yosemite), install TunTap using this
[installation package](http://tuntaposx.sourceforge.net/download.xhtml){:.external}.

For macOS 10.13 (High Sierra) and later versions, do the following:

1.  Install [Homebrew](https://brew.sh){:.external}:

    ```posix-terminal
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
    ```

1.  Install TunTap:

    ```posix-terminal
    brew cask install tuntap
    ```

The installation of TunTap may fail at first. In that case, do the following:

1.  Open `System Preferences`.
1.  Open `Security & Privacy` and select the`General` tab.
1.  Next to the `System software from developer "Mattias Nissler" was blocked
    from loading.` message, click **Allow** (see Apple's
    [User-Approved Kernel Extension Loading](https://developer.apple.com/library/archive/technotes/tn2459/_index.html){:.external}
    for details).
1.  Run the install command again:

    ```posix-terminal
    brew cask install tuntap
    ```

After installing TunTap, run the following command:

```posix-terminal
sudo chown $USER /dev/tap0
```

#### Start the emulator

To start the emulator with networking enabled, run the following command:

```posix-terminal
fx emu -N
```

Note: If you need to reach the internet from the emulator, configure IP
forwarding and IPv4 support on the emulator TAP interface.

## Pave the device with Fuchsia

In a new terminal, pave the device with your Fuchsia image:

```posix-terminal
fx serve
```

See [Serve a build](/docs/development/build/fx.md#serve-a-build) for more
information.

## Explore Fuchsia {#explore-fuchsia}

When Fuchsia is booted and displays the `$` prompt in the shell, you can now run
[components](/docs/concepts/components/v2). In Fuchsia, components are the basic
unit of executable software.

To run components on your Fuchsia device, see
[Run an example component](/docs/development/run/run-examples.md).

### Run shell commands

To shutdown or reboot Fuchsia, use the following `dm` commands in the shell:

```sh
dm shutdown
dm reboot
```

See
[Connect to a target shell](/docs/development/build/fx.md#connect-to-a-target-shell)
for more information.

### Select a tab {#select-a-tab}

Fuchsia shows multiple tabs in the shell. At the top of the screen, the
currently selected tab is highlighted in yellow.

The following keyboard shortcuts help you navigate the terminal:

-   Alt+Tab switches between tabs.
-   Alt+F{1,2,...} switches directly to a tab.
    -   Tab zero is the console, which displays the boot and component log.
    -   Tabs 1, 2 and 3 contain shells.
    -   Tabs 4 and higher contain components you've launched.
-   Alt+Up/Down scrolls up and down by lines.
-   Shift+PgUp/PgDown scrolls up and down by half page.
-   Ctrl+Alt+Delete reboots.

### Write software for Fuchsia

For an example of writing [FIDL](/docs/development/languages/fidl) APIs and client
and server components, review the
[FIDL tutorials](/docs/development/languages/fidl/tutorials/overview.md)

## Run tests

To test Fuchsia on your device, see
[Running tests as components](/docs/development/testing/running_tests_as_components.md).

## Launch a graphical component

Most graphical components in Fuchsia use the
[Scenic](/docs/concepts/graphics/scenic/scenic.md) system compositor. You can
launch such components (commonly found in `/system/apps`) using the
`present_view` command, for example:

```sh
present_view fuchsia-pkg://fuchsia.com/spinning_square_view#meta/spinning_square_view.cmx
```

See [Scenic example apps](/src/ui/examples).

If you launch a component that uses Scenic or hardware-accelerated graphics,
Fuchsia enters the graphics mode, which doesn't display the shell. To use the
shell, press `Alt+Escape` to enter the console mode. In the console mode,
`Alt+Tab` has the same behavior described in [Select a tab](#select-a-tab).
Press `Alt+Escape` again to return to the graphics mode.

## Contribute changes

To submit your contribution to Fuchsia, see
[Contribute changes](/docs/development/source_code/contribute_changes.md).

## See also

*   [fx workflows](/docs/development/build/fx.md)
*   [Workflow tips and questions](/docs/development/source_code/workflow_tips_and_faq.md)
*   [Configure editors](/docs/development/editors/)
*   [Source code layout](/docs/concepts/source_code/layout.md)
*   [Build system](/docs/concepts/build_system/index.md)

