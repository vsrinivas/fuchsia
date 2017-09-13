# grubdisk

The scripts contained within are a small set of tools that reliably build a disk
that can boot Zircon systems from a GPT EFI System Partition on the same
machine. This is a convenience for booting Zircon/Fuchsia on BIOS based
systems, while still using a GPT layouts.

## Quick & Easy

```
source scripts/env.sh
fbuild
./scripts/grubdisk/build-all.sh
```

The output will be `out/grub.raw`, which may be installed as a disk to any BIOS
based computer, such as a compute VM. The configuration will search for a GPT
ESP partition on the system containing `zircon.bin` and `bootdata.bin` and boot
it.

## Step by Step Usage

### Build grub

In your Fuchsia checkout:

```
source scripts/env.sh
fbuild
./third_party/grubdisk/build-grub.sh
```

### Allocate a disk image (or physical disk)

```
# Linux:
fallocate -l 1m out/grub.raw
# OSX:
mkfile -nv 1m out/grub.raw
# POSIX:
head -c 1048576 /dev/zero > out/grub.raw

# Physical disk:
sudo chown $USER /dev/target_disk_device
```

### Make a grub core.img

```
./third_party/grubdisk/build-core.sh
```

### Embed grub into the disk

Substitute `grub.raw` for your disk device if you are targeting a real disk.

```
./third_party/grubdisk/embed.sh out/grub.raw
```

## Technical details

The tools contained here simply drive grub and implement a pure-userspace
host-ignorant equivalent to grub-install(1). The standard grub tools have a
challenging propensity of reading information from the host system in order to
detect how to install grub. While this is convenient for your average GNU/Linux
user, it is a pain for embedded/alternative use cases. If you read documentation
on how people normally do the kind of work these tools do, they end up with
loopback mounts, chroots and so on.

This tool creates a protective MBR, a GPT, and a single BIOS partition. It
installs the grub boot.img into the PMBR Boot Code, and then installs the
core.img into the BIOS partition it created.

The core.img that is created contains an embedded grub.cfg that searches the
system GPTs for a partition containing `zircon.bin`. Once it finds that, it
will multiboot `zircon.bin` with `bootdata.bin`.

## TODO

 * Support adding a BIOS partition to a preexisting GPT, instead of emptying out
   the GPT and writing only one.
