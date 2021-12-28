# Fuchsia emulator

The Fuchsia emulator (FEMU) is the default emulator for Fuchsia. FEMU allows you
to test Fuchsia components and applications without a physical Fuchsia device.

FEMU is included in the Fuchsia source tree. FEMU is downloaded (or updated) by
`jiri`, as part of `jiri update` or `jiri run-hooks`, and is fetched into the
`/prebuilt/third_party/aemu` directory of your Fuchsia source tree.

You can launch FEMU using the `fx vdl` command, or using the `fvdl` command in
the Fuchsia SDK.

## FEMU, AEMU, and QEMU {#femu-aemu-and-qemu}

FEMU is based on the
[Android Emulator (AEMU)](https://developer.android.com/studio/run/emulator){:.external},
which is a fork of [QEMU](https://www.qemu.org/){:.external} – in some
instances, such as [emulating Zircon](#emulating-zircon), you need to use QEMU
instead.

Due to legacy issues, there may be references to AEMU in the code and
documentation.

### Emulating Zircon {#emulating-zircon}

If you only want to emulate Zircon, you must use `fx qemu` instead. Read
[Debugging the Kernel using QEMU](/docs/development/debugging/qemu.md) to learn
more. This is for kernel developers. Most Fuchsia developers do not need to use
this workflow.

## Features

FEMU looks and behaves like a Fuchsia device, except that no paving or flashing
is required with FEMU.

The features of FEMU include:

*   **GUI Support:** You can run Fuchsia with the GUI (by default) or without
    the GUI (using the `--headless` argument).
*   **GPU Support:** You can run with the host’s GPU (by default) with full
    [Vulkan](/docs/development/graphics/magma/concepts/vulkan.md){:.exyernal} support, or
    you can choose software rendering using
    [SwiftShader](https://swiftshader.googlesource.com/SwiftShader/){:.external}.
*   **Remote Development:** You can use a remote desktop with FEMU, either with
    Chrome Remote Desktop or from the command line using
    [fx emu-remote](https://fuchsia.dev/reference/tools/fx/cmd/emu-remote)
    command.

To see full list of supported flags:

```posix-terminal
fx vdl start --help
```

The Fuchsia SDK's `fvdl` command supports the same flags as `fx vdl`.

## Image and board support {#image-and-board-support}

When setting up FEMU using `fx set`, FEMU supports the following boards:

*   `qemu-x64`
*   `qemu-arm64`

With the Fuchsia SDK, FEMU supports the following pre-built images:

*   `qemu-x64`
*   `workstation.qemu-x64-release`
*   `qemu-arm64`

ARM64 support (`qemu-arm64`) is very limited and not recommended.

## Networking

On Linux, FEMU should generally be run with the `-N` flag that
provides networking through an emulated NIC.

Note: Instructions for setting up
networking for FEMU is in the
[Start the Fuchsia Emulator](/docs/get-started/set_up_femu.md) guide.

Without `-N`, your emulator is not discoverable using `ffx target list`
(or`fx list-devices`). However, you can manually set the SSH address and
use `fx` tools to interact with your emulator.

If starting the emulator without `-N` (that is, `fx vdl start`), an available TCP
port from the host is picked and forwarded to the emulator's SSH port. When
the emulator launches successfully, an additional instruction, which uses
`fx set-device` with the correct SSH port, is printed in the terminal output:

```posix-terminal
fx set-device 127.0.0.1:{{ '<var>' }}SSH_PORT{{ '</var>' }}
```
Using ths command above,  you can manually set the SSH device.

To verify that your `fx` tool is using the correct port, run the
following command:

```posix-terminal
fx status
```

You should see the SSH address printed next to `Device name`.

To SSH into the emulator, run the following command:

```posix-terminal
fx ssh
```

## Unsupported CPUs {#unsupported-cpu}

FEMU currently does not run on:

* ARM64 processors, including the Apple M1 processor.
* AMD processors.

## Supported hardware for graphics acceleration {#supported-hardware}

FEMU currently supports a limited set of GPUs on macOS and Linux for
hardware graphics acceleration. FEMU uses a software renderer fallback
for unsupported GPUs.


<table>
  <tbody>
    <tr>
      <th>Operating System</th>
      <th>GPU Manufacturer</th>
      <th>OS / Driver Version</th>
    </tr>
    <tr>
      <td>Linux</td>
      <td>Nvidia Quadro</td>
      <td>Nvidia Linux Drivers <a href="https://www.nvidia.com/download/driverResults.aspx/160175/en-us">440.100</a>+</td>
    </tr>
    <tr>
      <td>macOS</td>
      <td><a href="https://support.apple.com/en-us/HT204349#intelhd">Intel HD Graphics</a></td>
      <td>macOS version 10.15+</td>
    </tr>
    <tr>
      <td>macOS</td>
      <td>AMD Radeon Pro</td>
      <td>macOS version 10.15+</td>
    </tr>
  </tbody>
</table>

## Common usage {#common-usage}

To launch FEMU, complete the [Get started with Fuchsia](/docs/get-started/README.md) guide.

Alternatively, you can use the Fuchsia SDK and use pre-built system images.

Once you're able to launch FEMU, you can perform the following tasks:

*   [Test components](/docs/development/run/run-test-component.md)
*   [Run end-to-end tests](/docs/development/testing/run_an_end_to_end_test.md)
