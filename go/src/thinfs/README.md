# Thinfs

Thinfs is a collection of disk managment utilities which can be used to build a
filesystem.

Thinfs currently includes an implementation of:
 * FAT16 and FAT32

## Download Sources ##

### Download ThinFS sources ###

Thinfs should be included in the full Fuchsia checkout by default.

```shell
jiri import thinfs
jiri update
```

## Building ##

Thinfs will be built as a part of the Fuchsia build. Follow the instructions
on the Fuchsia landing pages.

To build Thinfs exclusively, the "--modules" parameter can be provided to gn.
This is useful if the rest of the build is broken, or if you're trying to
incrementally rebuild Thinfs alone.

```shell
cd $FUCHSIA_ROOT
./packages/gn/gen.py --modules=thinfs
./buildtools/ninja -C out/debug-x86-64
```

## Testing ##

### Running on Fuchsia ###

If you have a partition (either on a hard disk or USB) which is formatted as
FAT, it will be automatically detected by the Magenta kernel and mounted using
thinfs under "/volume".

If you want to manually mount a block device, use the following steps:

Use lsblk to determine which block device you'd like to mount.
```shell
lsblk
```

Let's arbitrarily choose block device '000'. If it is formatted as FAT, you can
skip to the mounting stage immediately. Otherwise, you'll need to format it as a
FAT filesystem (which, as a warning, will wipe all data present on the
partition).

To format the block device:
```shell
mkfs /dev/class/block/000 fat
```

To mount the block device:
```shell
mount /dev/class/block/000 /mount/path/of/your/choice
```
