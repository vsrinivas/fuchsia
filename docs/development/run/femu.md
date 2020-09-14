# Fuchsia Emulator (FEMU)

Fuchsia can [run under emulation](/docs/getting_started.md#set_up_the_emulator)
using Fuchsia Emulator (FEMU).

## Prebuilt FEMU

FEMU is downloaded by `jiri` as part of `jiri update` or `jiri run-hooks`.

FEMU is fetched into `//prebuilt/third_party/aemu`. You can run it using `fx emu`
(see section ["Run Fuchsia under FEMU"](#run_fuchsia_under_femu)).

## Network setup

In order to enable FEMU to connect to the local update server, you need to set
up a persistent TUN/TAP device used by FEMU to create emulator network interface
in advance.

### Linux network setup

On Linux, run the following commands to setup the device and interface:

```
sudo ip tuntap add dev qemu mode tap user $USER
sudo ifconfig qemu up
```

### macOS network setup

macOS does not support TUN/TAP devices out of the box; however, there is a widely
used set of kernel extensions called
[tuntaposx](http://tuntaposx.sourceforge.net/download.xhtml){:.external} that allow
macOS to create virtual network interfaces.

For macOS 10.9 (Mavericks) and 10.10 (Yosemite), install TunTap using this
[installation package](http://tuntaposx.sourceforge.net/download.xhtml){:.external}.

For macOS 10.13 (High Sierra) and later versions, do the following:

1.  Install [Homebrew](https://brew.sh){:.external}:

    ```posix-terminal
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
    ```

1.  Install TunTap:

    ```posix-terminal
    brew cask install tuntap
    ```

The installation of TunTap may fail at first. In that case, do the following:

1.  Open `System Preferences`.
1.  Open `Security & Privacy` and select the`General` tab.
1.  Next to the `System software from developer "Mattias Nissler" was blocked
    from loading.` message, click **Allow** (see Apple's
    [User-Approved Kernel Extension Loading](https://developer.apple.com/library/archive/technotes/tn2459/_index.html){:.external}
    for details).
1.  Run the install command again:

    ```posix-terminal
    brew cask install tuntap
    ```

After installing TunTap, run the following command:

```posix-terminal
sudo chown $USER /dev/tap0
```

## Run Fuchsia under FEMU

Ensure that you have set up and built the Fuchsia product (this example uses
`workstation` product). Currently only `qemu-x64` boards are supported.

```posix-terminal
cd $FUCHSIA_DIR

fx set workstation.qemu-x64 --release [--with=...]

fx build
```

Then run Fuchsia Emulator with ssh access:

```posix-terminal
cd $FUCHSIA_DIR

fx emu -N
```

To exit FEMU, run `dm poweroff` in the FEMU terminal.

### Run FEMU without GUI

If you don't need graphics or working under the remote workflow,
you can run FEMU in headless mode by adding `--headless` argument to the
`fx emu` command, for example:

```posix-terminal
cd $FUCHSIA_DIR

fx emu -N --headless
```

### Specify GPU used by FEMU

By default, FEMU tries using the host GPU automatically if it is available,
and will fall back to software rendering using
[SwiftShader](https://swiftshader.googlesource.com/SwiftShader/) if host GPU is
unavailable.

You can also add argument `--host-gpu` or `--software-gpu` to `fx emu` command
to enforce FEMU to use a specific graphics device.

### Develop remotely

There are multiple ways to develop Fuchsia remotely using FEMU, and all the
workflow supports GPU acceleration without requiring GPU drivers and runtime
on the local machine.

#### Remote Desktop

These instructions require remote access to the remote machine using remote
desktop tools like [Chrome Remote Desktop](https://remotedesktop.google.com/).

Follow general remote desktop instructions for logging in to your remote machine.
Once logged in, follow standard Fuchsia development instructions for building
Fuchsia, and use the following command to run Fuchsia in the emulator inside
your remote desktop session:

```posix-terminal
cd $FUCHSIA_DIR

fx emu
```

The emulator will use Swiftshader instead of real GPU hardware for GPU
acceleration when running inside Chrome Remote Desktop. Performance will be
worse than using real GPU hardware but the benefit is that it runs on any
workstation.

#### `fx emu-remote` Command

These instructions work for local machines with macOS or Linux, and require SSH
access to a Linux workstation capable of building Fuchsia.

On the terminal of your local machine, type the following to build, fetch the
artifacts and start the emulator with networking:

```posix-terminal
cd $FUCHSIA_DIR

fx emu-remote {{ '<var>REMOTE-WORKSTATION-NAME</var>' }}  -- -N
```

Alternatively, start the emulator on remote workstation, and open an WebRTC
connection to it using local browser.

On the terminal of your local machine, type the following to start the emulator
with networking on {{ '<var>REMOTE-WORKSTATION-NAME</var>' }} and have the
output forwarded using WebRTC to a Chrome tab on local machine:

```posix-terminal
cd $FUCHSIA_DIR

fx emu-remote --stream {{ '<var>REMOTE-WORKSTATION-NAME</var>' }} -- -N
```

This by default uses software rendering and GPU acceleration is supported by
using an existing X server on the remote machine with access to GPU hardware:

```posix-terminal
cd $FUCHSIA_DIR

fx emu-remote --stream --display :0 {{ '<var>REMOTE-WORKSTATION-NAME</var>' }} -- -N
```

Any arguments after “--” will be passed to the fx emu invocation on the remote
machine.

