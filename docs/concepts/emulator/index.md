# Fuchsia emulator (FEMU)

The Fuchsia emulator (FEMU) allows you to test Fuchsia components and applications without needing a Fuchsia device.
FEMU is included in Fuchsia source, and it’s downloaded by `jiri` as part of `jiri update` or `jiri run-hooks`.
It’s fetched into the Fuchsia directory `/prebuilt/third_party/aemu`.

You can call FEMU with `fx` using the `fx emu` command, or from the Fuchsia IDK using `femu.sh`.


## FEMU and other emulators {#femu-and-other-emulators}

FEMU is the default emulator for Fuchsia. FEMU is based on the
[Android Emulator (AEMU)](https://developer.android.com/studio/run/emulator), which is a fork of
[QEMU](https://www.qemu.org/). Due to legacy issues, there may be references to AEMU in the code and documentation.

In some instances, such as [emulating Zircon](#emulating-zircon), you must use QEMU instead.


## FEMU Features {#femu-features}

FEMU looks and behaves like a Fuchsia device, with the exception that no paving is required.

FEMU features include:

*   **GUI Support:** You can run Fuchsia with the GUI (by default) or without the GUI
    (using the `--headless` argument with the [fx emu](https://fuchsia.dev/reference/tools/fx/cmd/emu) command)
*   **GPU Support:** You can run with the host’s GPU (by default) with full
    [Vulkan](/docs/concepts/graphics/magma/vulkan.md) support, or you can choose
    software rendering using [SwiftShader](https://swiftshader.googlesource.com/SwiftShader/).
*   **Remote Development:** You can use a remote desktop with FEMU, either with Chrome Remote Desktop
     or from the command line using [fx emu-remote](https://fuchsia.dev/reference/tools/fx/cmd/emu-remote)
     command or `femu.sh` with the Fuchsia IDK.

To configure these features, see the [Running Fuchsia Emulator](/docs/development/run/femu.md)
page. Additional features are listed in the [fx emu](https://fuchsia.dev/reference/tools/fx/cmd/emu) reference page.
If you’re using the Fuchsia IDK, `femu.sh` supports the same flags as `fx emu`.


## FEMU limitations {#femu-limitations}

### FEMU image and board support {#femu-image-and-board-support}

When setting up FEMU using `fx emu`, FEMU only supports the following boards:

*   `qemu-x64`
*   `qemu-arm64`

When using the Fuchsia IDK to set up FEMU, you are limited to the following pre-built images:

*   `qemu-x64`
*   `workstation.qemu-x64-release`
*   `qemu-arm64`


### FEMU discoverability {#femu-discoverability}

When looking for an emulated device using the Fuchsia SDK, you have to specify
the `--netboot` option. For example, searching for devices using `device-finder`,
you need to use the following command:

```none
device-finder list -netboot
```

See [device-finder](/docs/development/sdk/documentation/device_discovery.md) for
more information about finding Fuchsia devices.

### FEMU networking  {#femu-networking}

The Fuchsia Emulator should generally be run with the `-N` flag that provides networking through an
emulated NIC. Instructions for setting up networking for FEMU is in
[Setting up the Fuchsia Emulator](/docs/get-started/set_up_femu.md).

Without networking, you only have an isolated serial console. With networking,
your device is visible to other tools such as `ssh` and `fx serve`.


### Emulating Zircon {#emulating-zircon}

If you only want to emulate Zircon, you must use `fx qemu` instead. Read
[Debugging the Kernel using QEMU](/docs/development/debugging/qemu.md) to
learn more. This is for kernel developers. Most Fuchsia developers do not need
to use this workflow.


## FEMU common usage  {#femu-common-usage}

To use FEMU, you must first
[download the Fuchsia source](/docs/get-started/get_fuchsia_source.md)
and [build Fuchsia](/docs/get-started/build_fuchsia.md).

Alternatively, you can use the Fuchsia IDK and use pre-built system images.

Then you can use FEMU to do the following:

*   [Set up and configure FEMU](/docs/get-started/set_up_femu.md)
*   [Run FEMU](/docs/development/run/femu.md)
*   [Test components](/docs/development/run/run-components-in-a-test.md)
*   [Run end-to-end tests](/docs/development/testing/run_an_end_to_end_test.md)
