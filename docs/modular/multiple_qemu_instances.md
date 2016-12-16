# Running multiple QEMU instances w/ network

Running Fuchsia on multiple instances of QEMU on the same machine with network
is useful for developing multi-device features.

## Instructions for running a single QEMU instance

Instructions for running Fuchsia on QEMU are [here](https://fuchsia.googlesource.com/magenta/+/master/docs/qemu.md) and for setting up network access in [netstack](https://fuchsia.googlesource.com/netstack/+/master/README.md).

## Instructions for running two QEMU instances w/ network

1. Create two [TAP interfaces](https://fuchsia.googlesource.com/magenta/+/master/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only), say `qemu0` and `qemu1`.
1. Choose a *different* IPv4 subnet to use for `qemu0` and `qemu1` like [here](https://fuchsia.googlesource.com/netstack/+/master/README.md#Using-netstack-on-qemu).
1. Add two separate entries in `/etc/dnsmasq.conf` for `qemu0` and `qemu1` like [here](https://fuchsia.googlesource.com/netstack/+/master/README.md#Setting-up-dnsmasq).
1. Setup NAT for both the interfaces like [here](https://fuchsia.googlesource.com/netstack/+/master/README.md#Setting-up-NAT-on-Linux).
1. Run using qemu startup script with -N for networking and -I to specify the interface to use.

```
$ scripts/run-magenta-x86-64 -N -I qemu0 -x out/debug-x86-64/user.bootfs
$ scripts/run-magenta-x86-64 -N -I qemu1 -x out/debug-x86-64/user.bootfs
```
