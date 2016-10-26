# Qemu

## Build Qemu

If you don't want to install in /usr/local (the default), which will require you
to be root, add --prefix=/path/to/install  (perhaps $HOME/qemu) and then you'll
need to add /path/to/install/bin to your PATH.

```
cd $SRC
git clone https://fuchsia.googlesource.com/third_party/qemu
cd qemu
./configure --target-list=arm-softmmu,aarch64-softmmu,x86_64-softmmu
make -j32
sudo make install
```

## Run Magenta under Qemu

```
# for aarch64
./scripts/run-magenta-arm64

# for x86-64
./scripts/run-magenta-x86-64
```

The -h flag will list a number of options, including things like -b to rebuild first
if necessary and -g to run with a graphical framebuffer.

To exit qemu, enter Ctrl-a x. Use Ctrl-a h to see other commands.

## Enabling Networking under Qemu (x86-64 only)

The run-magenta-x86-64 script, when given the -N argument will attempt to create
a network interface using the Linux tun/tap network device named "qemu".  Qemu
does not need to be run with any special privileges for this, but you need to
create a persistent tun/tap device ahead of time (which does require you be root):

On Linux:

```
sudo apt-get install uml-utilities
sudo tunctl -u $USER -t qemu
sudo ifconfig qemu up
```

This is sufficient to enable link local IPv6 (as the loglistener tool uses).

On macOS:

macOS does not support tun/tap devices out of the box, however there is a widely
used set of kernel extensions called tuntaposx which can be downloaded
[here](http://tuntaposx.sourceforge.net/download.xhtml). Once the installer
completes, the extensions will create up to 16 tun/tap devices. The
run-magenta-x86-64 script uses /dev/tap0.

```
sudo chown $USER /dev/tap0

# Run magenta in QEMU, which will open /dev/tap0
./scripts/run-magenta-x86-64 -N

# (In a different window) bring up tap0 with a link local IPv6 address
sudo ifconfig tap0 inet6 fc00::/7 up
```

**NOTE**: One caveat with tuntaposx is that the network interface will
automatically go down when QEMU exits and closes the network device. So the
network interface needs to be brought back up each time QEMU is restarted.
