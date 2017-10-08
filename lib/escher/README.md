# Escher

Escher is a physically based renderer.

## Features

 * Volumetric soft shadows
 * Color bleeding
 * Light diffusion
 * Lens effect

## Building for Fuchsia
Escher is part of the default Fuchsia build.  The "waterfall" demo is installed
as `system/apps/waterfall`.

## Building for Linux
Escher can also build on Linux.  In order to do so, you need to:
  * add the Jiri "escher_linux_dev" manifest, then Jiri update
    ```
    cd $FUCHSIA_DIR
    jiri import escher_linux_dev https://fuchsia.googlesource.com/manifest
    jiri update
    ```
  * install build dependencies
    ```
    sudo apt install libxinerama-dev libxrandr-dev libxcursor-dev libx11-xcb-dev libx11-dev mesa_common_dev
    ```
  * install a GPU driver that supports Vulkan
    * NVIDIA: version >= 367.35
    * Intel: Mesa >= 12.0 ([installation instructions](https://stackoverflow.com/questions/40783620/how-to-install-intel-graphics-drivers-with-vulkan-support-for-ubuntu-16-04-xen/40792607#40792607))
  * install the [LunarG Vulkan SDK](https://lunarg.com/vulkan-sdk/) (installed
    into third_party/vulkansdk when Escher is pulled down by jiri as a part of Fuchsia)
  * set the VULKAN_SDK, VK_LAYER_PATH, and LD_LIBRARY_PATH environment variables, e.g.:
    ```
    export VULKAN_SDK=$FUCHSIA_DIR/lib/escher/third_party/vulkansdk/x86_64
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$VULKAN_SDK/lib
    export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d
    ```
  * pull down dependencies for the waterfall example:
    ```
    (cd examples/common/third_party; git submodule init; git submodule update)
    ```
  * specify that you want to build only the Escher module:
    ```
    cd $FUCHSIA_DIR
    fset x86-64 --release --packages garnet/packages/escher_linux
    fgen
    ```
    * The `fset` and `fgen` commands are provided by the `env.sh` script; see `$FUCHSIA_DIR/docs/getting_started.md`.
    * There may be a spurious error from `fgen` regarding `skia_use_sfntly`; ignore it.
    * NOTE!! These commands may conflict with the Vulkan SDK on your LD_LIBRARY_PATH.  It is probably best to run these commands in one terminal window, then switch to another and setting LD_LIBRARY_PATH before Building
    and running.
  * BUILD!! AND RUN!!!
    ```
    buildtools/ninja -C out/release-x86-64/ && out/release-x86-64/host_x64/waterfall
    ```
