# Configure and build Fuchsia {#configure-and-build-fuchsia}

This guide provide instructions on how to set up and build Fuchsia.

The steps are:

1. [Prerequisites](#prerequisites).
1. [Set your build configuration](#set-your-build-configuration).
1. [Speed up the build (Optional)](#speed-up-the-build).
1. [Build Fuchsia](#build-fuchsia).

## 1. Prerequisites {#prerequisites}

### Source code {#source-code}

Before you start, complete the
[Download the Fuchsia source code](/docs/get-started/get_fuchsia_source.md)
guide to download the Fuchsia source code and set up the Fuchsia development
environment on your machine.

### Hardware requirements {#hardware-requirements}

Fuchsia can only be built on a machine with one of the following CPU
architecture and operating system combinations:

- x86-64 Linux (Debian-based distributions only)
- x86-64 macOS

Windows and ARM64 Linux are not supported.

ARM64 macOS is not officially supported, but a build may still succeed if
commands are run in a terminal started with:

```posix-terminal
arch -x86_64 /bin/zsh
```

## 2. Set your build configuration {#set-your-build-configuration}

Fuchsia's build configuration informs the build system which product to
build and which architecture to build for.

To set your Fuchsia build configuration, run the following
[`fx set`][fx-set-reference] command:

<pre class="prettyprint">
<code class="devsite-terminal">fx set <var>PRODUCT</var>.<var>BOARD</var></code>
</pre>

Replace the following:

* `PRODUCT`: The Fuchsia product that you want to build; for example, `core` and
  `workstation`.
* `BOARD`: The architecture of the product; for example, `x64` and `qemu-x64`

The example command below sets a build configuration that builds the `core`
product for the Fuchsia emulator (FEMU):

```posix-terminal
fx set core.qemu-x64
```

In this example:

  * `core` is a product with the minimum feature set of Fuchsia, including
     common network capabilities.
  * `qemu-x64` is a board that refers to the x64 architecture on FEMU, which
     is based on the open source emulator QEMU.

Additionally, the example command below sets your build configuration to build
Fuchsia's `workstation` product for an `x64` architecture machine or device:

Note: For more information on Workstation, see
[Build Workstation][build-workstation].

```posix-terminal
fx set workstation.x64
```

For more information on the build configuration,
see [Configure a build](/docs/development/build/fx.md#configure-a-build).

## 3. Speed up the build (Optional) {#speed-up-the-build}

Note: This step is not required to build Fuchsia, but it's recommended
since it can save you a lot of time when you build Fuchsia.

To speed up the Fuchsia build, you can use one of the following services:

*   [Enable Goma](#enable-goma)
*   [Install ccache](#install-ccache)

### Enable Goma {#enable-goma}

[Goma](https://chromium.googlesource.com/infra/goma/server/){:.external} is a
distributed compiler service for open source projects such as Chrome, Android
and Fuchsia.

If you have access to Goma, enable a Goma client on your machine:

```posix-terminal
fx goma
```

### Install ccache {#install-ccache}

If you do not have access to Goma, but want to accelerate the Fuchsia build
locally, use <code>[ccache](https://ccache.dev/){:.external}</code> to cache
artifacts from previous builds.

* {Linux}

  To use `ccache` on Linux, install the following package:

  ```posix-terminal
  sudo apt-get install ccache
  ```
* {macOS}

  For macOS, see
  [Using CCache on Mac](https://chromium.googlesource.com/chromium/src.git/+/HEAD/docs/ccache_mac.md){:.external}
  for installation instructions.

`ccache` is enabled automatically if your `CCACHE_DIR` environment variable
refers to an existing directory.

To override this default behavior, specify the following flags to `fx set`:

*   Force the use of `ccache` even when other accelerators are available:

    <pre class="prettyprint">
    <code class="devsite-terminal">fx set <var>PRODUCT</var>.<var>BOARD</var> --ccache</code>
    </pre>

*   Disable the use of `ccache`:

    <pre class="prettyprint">
    <code class="devsite-terminal">fx set <var>PRODUCT</var>.<var>BOARD</var> --no-ccache</code>
    </pre>

## 4. Build Fuchsia {#build-fuchsia}

The [`fx build`][fx-build-reference] command executes the build to transform
source code into packages and other build artifacts.

To build Fuchsia, run the following command:

Note: Building Fuchsia can take up to 90 minutes.

```posix-terminal
fx build
```

When you modify source code, run the `fx build` command again to perform an
incremental build, or run the `fx -i build` command to start a watcher, which
automatically builds whenever you update the source code.

For more information on building Fuchsia,
see [Execute a build](/docs/development/build/fx.md#execute-a-build).

## Next steps

To launch the Fuchsia emulator (FEMU) on your machine, see
[Start the Fuchsia emulator](/docs/get-started/set_up_femu.md).

However, if you want to run Fuchsia on a hardware device, see
[Install Fuchsia on a device](/docs/development/hardware/paving.md) instead.

<!-- Reference links -->

[build-workstation]: /docs/development/build/build_workstation.md
[fx-set-reference]: https://fuchsia.dev/reference/tools/fx/cmd/set
[fx-build-reference]: https://fuchsia.dev/reference/tools/fx/cmd/build
