# Recovery Netstack Hacking HOWTO

This document describes how to run Netstack3, as well as a few possible dev
workflows. The instructions are for how to set up QEMU, but are also applicable
to real hardware. The instructions here are for running Netstack3 as a 
*replacement* netstack. If you want to run Netstack3 alongside the default
netstack, but bound to a specific interface, check the instructions in
[enclosed_runner](../tools/enclosed_runner/README.md).

## (Step 1) QEMU Setup

First, set up two interfaces for QEMU to use (you only need to do this once per
boot of host machine):

```
sudo tunctl -u $USER -t qemu
sudo tunctl -u $USER -t qemu-extra
sudo ifconfig qemu up
sudo ifconfig qemu-extra up
```

If you want your workstation to have an IPv4 address for the qemu and/or
qemu-extra interfaces, assign that now:

```
sudo ifconfig qemu 192.168.1.21 netmask 255.255.255.0 up
sudo ifconfig qemu-extra 192.168.1.22 netmask 255.255.255.0 up
```

When you want to run QEMU with two devices, use the following invocation of
`fx run`:

```
fx run -kN -- -netdev type=tap,ifname=qemu-extra,script=no,downscript=no,id=net1 -device e1000,netdev=net1,mac=52:54:00:63:5e:7b
```

## (Step 2) `sysmgr` Setup

No matter how you want to run `netstack3`, you'll probably want to disable the
Go `netstack` first, so as to avoid conflicts and confusion between the two
stacks.

In [`garnet/bin/sysmgr/config/services.config`](../sysmgr/config/services.config):

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
which will have those packages be part of the base system.

## Running

Once you've done this setup, the netstack should be set up. You can run fuchsia
however you normally would, then use `net_cli` to set up the interface:

```
net_cli if add /dev/class/ethernet/000
net_cli if addr add 1 192.168.1.39 24
net_cli fwd add-device 1 192.168.1.0 24
```

If you want, you can take advantage of the dual-interface qemu setup to run the
`net_cli` commands automatically. For instance, here's a script that automates
the build-run-setup loop:

```
fx build &&
fx run -kN -- -netdev type=tap,ifname=qemu-extra,script=no,downscript=no,id=net1 -device e1000,netdev=net1,mac=52:54:00:63:5e:7b &&
$FUCHSIA_OUT_DIR/build-zircon/tools/netruncmd : "net_cli if add /dev/class/ethernet/000 && net_cli if addr add 1 192.168.1.39 24 && net_cli fwd add-device 1 192.168.1.0 24"
```

Once you've done this, you can check that Netstack3 is reachable by pinging it
from your host machine:

```
ping -I qemu-extra 192.168.1.39 -c 1
```

This is painfully slow right now - wesleyac@ is working on a solution that
pushes only the package, instead of rebuilding the image and restarting qemu
each time, but that's further off.
