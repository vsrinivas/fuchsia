# Recovery Netstack Hacking HOWTO

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

It's not necessary to set a static IPv4 address for the `qemu` interface if the
following invocation of `fx run` is used (which runs dnsmasq and will assign an
address of `192.168.3.1/24` to the interface):

```
fx run -kN -u scripts/start-dhcp-server.sh -- \
  -netdev type=tap,ifname=qemu-extra,script=no,downscript=no,id=net1 \
  -device e1000,netdev=net1,mac=52:54:00:63:5e:7b
```

At this point you should be able to `fx shell` and ping the default netstack
with the address assigned via DHCP (probably `192.168.3.53`).

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

### (Step 1) `sysmgr` Setup

To run Netstack3 as the replacement netstack, you'll probably want to disable
the Go `netstack` first, so as to avoid conflicts and confusion between the two
stacks.

In [`garnet/bin/sysmgr/config/services.config`](
../../../../../garnet/bin/sysmgr/config/services.config):

* Replace `fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx` on the
  `fuchsia.net.stack.Stack` line with
  `fuchsia-pkg://fuchsia.com/netstack3#meta/netstack3.cmx`
* Remove all of the lines from `services` referencing
  `fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx`.
* Remove `fuchsia.netstack.Netstack` from `startup_services` and
  `update_dependencies`.
* Note that trailing commas are not allowed, and will cause `sysmgr` to fail -
  make sure to check your config for that.

If you frequently work on Netstack3 like this, consider telling git to ignore
changes to `bin/sysmgr/config/services.config` - you can do this with the
command `git update-index --skip-worktree bin/sysmgr/config/services.config`.
This comes with the caveat that when you _do_ want to edit the file and check it
in (or someone else has made a breaking change to the config format), you need
to remember that you've done this, though. It can be undone by the same command
with the `--no-skip-worktree` flag.

**NOTE:** At the time of this writing, Netstack3 is not complete enough to allow
for dynamic package download and install. So make sure that every package that
you'll need is included in your `fx set` line using the `--with-base` argument,
which will have those packages be part of the base system.  Like this:

`fx set core.x64
 --with-base //garnet/packages/prod:net-cli \
 --with-base //garnet/packages/prod:netstack3 \
 --with-base //garnet/packages/prod:netcfg \
 --with-base //garnet/packages/prod:chrealm`

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
