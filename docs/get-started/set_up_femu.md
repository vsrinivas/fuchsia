# Set up the emulator

You can set up and run Fuchsia in an emulator,
also called FEMU.


## Prerequisites

To run FEMU, you must have

 * [Fuchsia source installed and environment variables created](/docs/get-started/get_fuchsia_source.md).
 * [Configured and built Fuchsia](/docs/get-started/build_fuchsia.md)


## Configure network

For Fuchsia's ephemeral software to work in the emulator, you need to configure
an IPv6 network.

  * [Linux configuration](#linux-config)
  * [macOS configuration](#mac-config)

### Linux {#linux-config}

To enable networking in FEMU, run the following commands:

```sh
sudo ip tuntap add dev qemu mode tap user $USER
sudo ip link set qemu up
```

### macOS {#mac-config}

You need to install
[TunTap](http://tuntaposx.sourceforge.net/index.xhtml){:.external}, kernel
extensions that allow macOS to create virtual network interfaces.

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

## Start the emulator

To start the emulator with networking enabled, run the following command:

```posix-terminal
fx emu -N
```

Note: If you need to reach the internet from the emulator, configure IP
forwarding and IPv4 support on the emulator TAP interface.

## Next steps

For next steps on using FEMU, see the [Running the Fuchsia Emulator](/docs/development/run/femu.md)
guide.
