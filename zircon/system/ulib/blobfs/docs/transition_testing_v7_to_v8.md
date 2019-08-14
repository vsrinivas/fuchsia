# V7 -> V8 Transition Testing

This document describes how to manually test the "version 7" to "version 8"
upgrade process.

## Start with v7 image:

### Host Machine

```
# Checkout master
git checkout origin/master
fx set bringup.x64 && fx build
truncate --size 64M blk.bin
./out/default.zircon/host-x64-linux-clang/obj/tools/blobfs/blobfs blk.bin@64M mkfs
# Checkout this patch
git checkout a-branch-with-this-patch
fx build && fx run -k -- -hda $PWD/blk.bin
```

### Target Machine

```
mount /dev/class/block/000 blob    # Expect failure; non-journaled. "Cannot upgrade..."
mount -j /dev/class/block/000 blob # Expect success (upgrade in logs).
umount /blob
mount -j /dev/class/block/000 blob # Expect success (normal mount).
umount /blob
mount /dev/class/block/000 blob    # Expect success (normal mount).
umount /blob
fsck /dev/class/block/000 blobfs    # Expect success.
fsck -j /dev/class/block/000 blobfs # Expect success.
```

## Start with v8 image:

### Host Machine

```
# Checkout this patch
git checkout a-branch-with-this-patch
fx set bringup.x64 && fx build
truncate --size 64M blk.bin
/out/default.zircon/host-x64-linux-clang/obj/tools/blobfs/blobfs blk.bin@64M mkfs
fx build && fx run -k -- -hda $PWD/blk.bin
```

### Target machine

```
mount /dev/class/block/000 blob    # Expect success (normal mount).
umount /blob
mount -j /dev/class/block/000 blob # Expect success (normal mount).
umount /blob
mount -j /dev/class/block/000 blob # Expect success (normal mount).
umount /blob
fsck /dev/class/block/000 blobfs    # Expect success.
fsck -j /dev/class/block/000 blobfs # Expect success.
```
