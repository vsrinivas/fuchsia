# Set up and start the Fuchsia emulator (FEMU)

This document describes how to set up and run the Fuchsia emulator (FEMU), including networking
and GPU support setup.

## Prerequisites

To run FEMU, you must have

 * [Fuchsia source installed and environment variables created](/docs/get-started/get_fuchsia_source.md)
 * [Configured and built Fuchsia](/docs/get-started/build_fuchsia.md)

### Building Fuchsia for FEMU

Before you can use FEMU, you need to build Fuchsia using `fx set`, 
specifying a qemu board and supported product. This example uses
`qemu-x64` for the board and `workstation` for the product:

<pre class="prettyprint">
<code class="devsite-terminal">fx set workstation.qemu-x64 --release [--with=...]</code>
<code class="devsite-terminal">fx build</code>
</pre>

Note: More information on supported boards and products is in the
[Fuchsia emulator overview](/docs/concepts/emulator/index.md).

## Configure network

For Fuchsia's ephemeral software to work with FEMU, you need to configure
an IPv6 network.

  * [Linux configuration](#linux-config)
  * [macOS configuration](#mac-config)

### Linux {#linux-config}

To enable networking in FEMU, run the following commands:

<pre class="prettyprint">
<code class="devsite-terminal">sudo ip tuntap add dev qemu mode tap user $USER</code>
<code class="devsite-terminal">sudo ip link set qemu up</code>
</pre>

Note: FEMU on Linux does not support external internet access.

### macOS {#mac-config}

Networking for FEMU is set up by default for macOS.


## Start FEMU

The most common way to run FEMU is with networking enabled, using the following commands.

### Linux {#linux-start-femu}

```posix-terminal
fx emu -N
```
Once you run the command, a separate window opens with the title "Fuchsia Emulator". You
can run shell commands in this window, just like you would on a Fuchsia device.

### macOS {#mac-start-femu}

```posix-terminal
fx vdl start --host-gpu
```

Note: When you launch FEMU for the first time on your Mac machine after starting up (ex: after a reboot),
a window pops up asking if you want to allow the process “aemu” to run on your machine.
Click “allow”.

To enable `fx tools` (like `fx ssh`) on macOS, run the following command:

```posix-terminal
fx set-device 127.0.0.1:${SSH_PORT} // where ${SSH_PORT} is a line printed in stdout
```

## Additional FEMU options

### Input options

By default FEMU uses a mouse pointer for input. You can add the argument `--pointing-device touch`
for touch input instead.

#### Linux

```posix-terminal
fx emu --pointing-device touch
```

#### macOS

```posix-terminal
fx vdl start --pointing-device touch
```

### Run FEMU without GUI support

If you don't need graphics or working under the remote workflow, you can run FEMU in headless mode:

#### Linux

```posix-terminal
fx emu --headless
```

#### macOS

```posix-terminal
fx vdl start --headless
```

### Specify GPU used by FEMU

By default, FEMU tries using the host GPU automatically if it is available, and falls
back to software rendering using [SwiftShader](https://swiftshader.googlesource.com/SwiftShader/)
if a host GPU is unavailable.

You can also add the argument `--host-gpu` or `--software-gpu` to the `fx emu` command
to force FEMU to use a specific graphics device. The commands and flags are listed below:

#### Linux

<table><tbody>
  <tr>
   <th>GPU Emulation method</th>
   <th>Explanation</th>
   <th><code>fx emu</code> flag</th>
  </tr>
  <tr>
   <td>hardware (host GPU) </td>
   <td>Uses the host machine’s GPU directly to perform GPU processing.</td>
   <td><code>fx emu --host-gpu</code></td>
  </tr>
  <tr>
   <td>software (host CPU)</td>
   <td>Uses the host machine’s CPU to simulate GPU processing.</td>
   <td><code>fx emu --software-gpu</code></td>
  </tr>
</tbody></table>

#### macOS

<table><tbody>
  <tr>
   <th>GPU Emulation method</th>
   <th>Explanation</th>
   <th><code>fx vdl</code> flag</th>
  </tr>
  <tr>
   <td>hardware (host GPU)</td>
   <td>Uses the host machine’s GPU directly to perform GPU processing.</td>
   <td><code>fx vdl start --host-gpu</code></td>
  </tr>
  <tr>
   <td>software (host CPU)</td>
   <td>Uses the host machine’s CPU to simulate GPU processing.</td>
   <td><code>fx vdl start --software-gpu</code></td>
  </tr>
</tbody></table>

### Supported hardware for graphics acceleration {#supported-hardware}

FEMU currently supports a limited set of GPUs on macOS and Linux for
hardware graphics acceleration. FEMU uses a software renderer fallback for unsupported GPUs.

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

## Exit FEMU

To exit FEMU, run `dm poweroff` in the FEMU terminal.

## Next steps

 *  To learn more about how FEMU works, see the
    [Fuchsia emulator (FEMU) overview](/docs/concepts/emulator/index.md).
 *  To learn more about Fucshia device commands and Fuchsia workflows, see
    [Explore Fuchsia](/docs/get-started/explore_fuchsia.md).

