# Workflow

## Fuchsia setup

Follow the general Fuchsia documents to obtain a Fuchsia world checkout and
learn how to run under qemu:

 - [Fuchsia Manifest](https://fuchsia.googlesource.com/manifest/+/master/README.md)
 - [Magenta Recipes](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md)

### Persistent file system

Having a persistent block device is handy, and you will probably want one.

In your `magenta` directory:

```sh
dd if=/dev/zero of=blk.bin bs=1M count=512
```

Then, run Fuchsia and:

```
> minfs /dev/class/block/000 mkfs
> minfs /dev/class/block/000 mount &
```

That's it. Only need to do it once, the device will be mounted automatically on
future boots.

### Networking

Networking is handly, and you will definitely want it. Follow [these
instructions](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md#Enabling-Networking-under-Qemu-x86_64-only).

(need to do this after each host reboot)

## Build and run

We provide a simple `tools/ledger` script that orchestrates basic tasks.

To build Fuchsia:

```sh
tools/ledger gn
tools/ledger build
```

To run Fuchsia (w/ persistent filesystem and networking):

```sh
tools/ledger run_fuchsia
```

To run Ledger tests (both unittests and apptests), run Fuchsia as above in a
separate host shell. In another shell, run:

```sh
tools/ledger test
```

and observe test results in the first shell.
