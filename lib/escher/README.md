# Escher

Escher is a physically based renderer.

## Features

 * Volumetric soft shadows
 * Color bleeding
 * Light diffusion
 * Lens effect

## Building
Escher requires Vulkan, and currently only builds on Linux.  In order to build Escher, you need:
  * a GPU and driver which support Vulkan (e.g. >= NVIDIA 367.35)
  * to install the [LunarG Vulkan SDK](https://lunarg.com/vulkan-sdk/)
  * set the VULKAN_SDK, VK_LAYER_PATH, and LD_LIBRARY_PATH environment variables
  * specify that you want to build only the Escher module:
    * ```cd $FUCHSIA_DIR; ./packages/gn/gen.py -m escher -r```
  * BUILD!!
    * ```buildtools/ninja -C out/release-x86-64/ && out/release-x86-64/host_x64/waterfall```
