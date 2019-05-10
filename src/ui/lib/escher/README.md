# Escher

Escher is a physically based renderer.

## Features

 * Volumetric soft shadows
 * Color bleeding
 * Light diffusion
 * Lens effect

## Building for Fuchsia
Escher is part of the default Fuchsia build.  The "waterfall" demo is not installed in `/system/bin`, but it used to be!

## Building for Linux
Escher can also build on Linux.  In order to do so, you need to:
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
    export VULKAN_SDK=$FUCHSIA_DIR/prebuilt/third_party/vulkansdk/linux/x86_64
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$VULKAN_SDK/lib
    export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d
    ```
  * Specify that you want the Escher examples to be built:
    ```
    cd $FUCHSIA_DIR
    fx set terminal.x64 --with //garnet/packages/examples:escher --args escher_use_null_vulkan_config_on_host=false
    ```
    * See `$FUCHSIA_DIR/docs/getting_source.md` for how to set up the `fx` tool.
    * Adding `--with //garnet/packages/examples:all` would also work, or anything else that includes `//garnet/packages/examples:escher`.  This should also work with any other product than `terminal`.
  * Do the following each time you want to rebuild and run the `waterfall` example:
    ```
    fx build host_x64/waterfall && out/default/host_x64/waterfall
    ```
