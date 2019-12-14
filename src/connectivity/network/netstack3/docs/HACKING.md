# Netstack3 Hacking HOWTO

This document describes how to run Netstack3, as well as a few possible dev
workflows. The instructions are for how to set up QEMU, but are also applicable
to real hardware.

## Running Alongside Default Netstack

The following instructions here are for running Netstack3 alongside the default
netstack on a separate interface. This is currently the recommended method of
running Netstack3.

### (Step 1) QEMU Setup

First, set up two interfaces for QEMU to use (you only need to do this once per
boot of host machine):

```
sudo ip tuntap add dev qemu mode tap user $USER
sudo ip tuntap add dev qemu-extra mode tap user $USER
sudo ifconfig qemu up
sudo ifconfig qemu-extra up
```

Assign an IPv4 address for the `qemu-extra` interface so that you can ping from
it later:

```
sudo ifconfig qemu-extra 192.168.4.1 netmask 255.255.255.0 up
```

Start Qemu with networking support:

```
fx qemu -N
```

At this point you should be able to `fx shell` and resolve the default
netstack interface address with `fx get-device-addr`.

### (Step 2) `enclosed_runner` Setup

Follow the instructions in the [`enclosed_runner` documentation](
../tools/enclosed_runner/README.md) to run Netstack3 on the second ethernet
interface. Note that the instructions above assigned `192.168.4.1/24` to
`qemu-extra` so the address assigned to Netstack3 through `enclosed_runner`
should also be in this subnet. If you're successful, you should be able to ping
the address assigned to Netstack3 (presumably `192.168.4.x`) from the host
machine.

## Running Netstack3 Only

The following instructions are for running Netstack3 as a replacement for the
default netstack.

### (Step 1) Build Configuration

`fx set ... --args=use_netstack3=true`

**NOTE:** At the time of this writing, Netstack3 is not complete enough to allow
for dynamic package download and install. So make sure that every package that
you'll need is included in your `fx set` line using the `--with-base` argument,
which will have those packages be part of the base system. Like this:

```
fx set core.x64
 --with-base //path/to/targetA \
 --with-base //path/to/targetB \
 --args=use_netstack3=true
```

### (Step 2) Running

Once you've done this setup, the netstack should be set up. You can run fuchsia
however you normally would, then use `net` to set up the interface:

```
net if add /dev/class/ethernet/000
net if addr add 1 192.168.1.39 24
net fwd add-device 1 192.168.1.0 24
```

Once you've done this, you can check that Netstack3 is reachable by pinging it
from your host machine:

```
ping -I qemu-extra 192.168.1.39 -c 1
```

This is painfully slow right now - wesleyac@ is working on a solution that
pushes only the package, instead of rebuilding the image and restarting qemu
each time, but that's further off.
