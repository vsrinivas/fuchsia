# Guest

The `guest` app enables booting a guest operating system using the Zircon
hypervisor.

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive linux guest system
see [Hypervisor Benchmarking](docs/benchmarking.md).

These instructions assume a general familiarity with how to netboot the target
device.

## Build host system with guest packages

```
$ cd $GARNET_DIR

# This will assemble all the boot images and start the bootserver. It will be
# ready to netboot once you see:
#
# [bootserver] listening on [::]33331
$ ./bin/guest/scripts/build.sh x86
```

## Running guests
After netbooting the target device, to run Zircon:

```
$ run zircon-guest
```

Likewise, to launch a Linux guest:
```
$ run linux-guest
```

## Running from Topaz
To run from topaz, update the build command as:

```
$ ./bin/guest/scripts/build.sh -p "topaz/packages/default,garnet/packages/linux-guest,garnet/packages/zircon-guest" x86
```

After netbooting the guest packages can be launched from the system launcher as
`linux-guest` and `zircon-guest`.
