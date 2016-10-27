# Developer Workflow

## Fuchsia setup

Follow the general Fuchsia documents to obtain a Fuchsia world checkout and
learn how to run under qemu:

 - [Fuchsia Manifest](https://fuchsia.googlesource.com/manifest/+/master/README.md)
 - [Magenta Recipes](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md)

### Persistent file system

Having a persistent block device is handy, and you will probably want one.
Follow [these
instructions](https://fuchsia.googlesource.com/magenta/+/master/docs/minfs.md)
to create a minfs partition.

### Networking

Networking is handy, and you will definitely want it. Follow [these
instructions](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md#Enabling-Networking-under-Qemu-x86_64-only).

You need to do this after each host reboot.

## Tests

To run Ledger tests (both unittests and apptests), run Fuchsia with networking
enabled in one shell, and in another shell run:

```sh
tools/ledger test
```

Test results will appear in the Fuchsia shell.
