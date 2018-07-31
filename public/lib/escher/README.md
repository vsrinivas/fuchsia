# Escher

Escher is a physically based renderer.

## Features

 * Volumetric soft shadows
 * Color bleeding
 * Light diffusion
 * Lens effect

## Building for Fuchsia
Escher is part of the default Fuchsia build.  The "waterfall" demo is installed
as `system/bin/waterfall`.

## Building for Linux
Escher can also build on Linux.  In order to do so, you need to:
  * add the Jiri "escher_linux_dev" manifest, then Jiri update
    ```
    cd $FUCHSIA_DIR
    jiri import escher_linux_dev https://fuchsia.googlesource.com/manifest
    jiri update
    ```
    * as part of the jiri update, Escher will download a private copy of the
      Vulkan SDK to $FUCHSIA_DIR/garnet/public/lib/escher/third_party/vulkansdk/
  * install build dependencies
    ```
    sudo apt install libxinerama-dev libxrandr-dev libxcursor-dev libx11-xcb-dev \
    libx11-dev mesa-common-dev
    ```
  * install a GPU driver that supports Vulkan
    * NVIDIA: version >= 367.35
      ```
      sudo apt install nvidia-driver
      ```
    * Intel: Mesa >= 12.0
      ```
      sudo apt install mesa-vulkan-drivers
      ```
  * set the VK_LAYER_PATH, and LD_LIBRARY_PATH environment variables, e.g.:
    ```
    export VULKAN_SDK=$FUCHSIA_DIR/garnet/public/lib/escher/third_party/vulkansdk/x86_64
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$VULKAN_SDK/lib
    export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d
    ```
  * specify that you want to build only Escher (+ examples/tests), for Linux:
    ```
    cd $FUCHSIA_DIR
    fx set x64  --packages garnet/packages/experimental/disabled/dev_escher_linux
    ```
    * See `$FUCHSIA_DIR/docs/getting_source.md` for how to set up the `fx` tool.
  * Do this once only (then you can skip to the next step for iterative development):
    ```
    fx full-build
    ```
  * BUILD!! AND RUN!!!
    ```
    buildtools/ninja -C out/x64/ && out/x64/host_x64/waterfall
    ```
