# Configure and build Fuchsia {#configure-and-build-fuchsia}

To build Fuchsia, you need to be able to run the `fx` command in your terminal.

Note: If you haven't set up your build environment, see
[Set up environment variables](/docs/get-started/get_fuchsia_source.md#set-up-environment-variables).

## Set build configuration

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

Note: This step is optional.

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

## Build Fuchsia

Note: Building Fuchsia can take up to 90 minutes.

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

## Next steps

See
[Explore Fuchsia](/docs/get-started/explore_fuchsia.md)
in the getting started guide to learn more about how Fuchsia is structured.
