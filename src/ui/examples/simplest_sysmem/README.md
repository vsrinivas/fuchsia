# Simplest Sysmem Example

This component contains 3 examples that demonstrates how to render a simple image using sysmem buffer allocation and Flatland API in C++.

* `--png`: Loads the smiley.png file from package data, and renders it on screen. This uses RGBA pixel format.
* `--rect`: Renders a fuchsia rectangle on screen using Flatland's `CreateFilledRect` API.
* `--block`: Generates 4 BGRA32 formatted colored blocks on screen.

NOTE: Currently `ffx session add` does not support passing arguments for `.cm` components (fxb/96004). Until the support is added in ffx session plugin, to change the input arg, you will need to modify `simplest_sysmem.cml`.

## Usage

```shell
$ fx set workstation_eng.qemu-x64 --with //src/ui/examples/simplest_sysmem
$ fx build
# Start package server (ex: fx serve)
# Start the emulator (ex: ffx emu)
# Login (don't forget the password you set)
$ ffx session add fuchsia-pkg://fuchsia.com/simplest_sysmem#meta/simplest_sysmem.cm

# Note: If you are modifying the example, you don't have to restart the emulator, iterate with:
$ ffx session restart
# Login again with the password you previously set
ffx session add fuchsia-pkg://fuchsia.com/simplest_sysmem#meta/simplest_sysmem.cm
```
