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
sudo ip link set qemu up
sudo ip link set qemu-extra up
```

Assign an IPv4 address for the `qemu-extra` interface so that you can ping from
it later:

```
sudo ip addr add dev qemu-extra 192.168.4.1/24
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
