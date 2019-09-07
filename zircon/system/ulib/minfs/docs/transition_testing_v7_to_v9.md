# V7 / V8 -> V9 Transition Testing

This document describes how to manually test the "version 7" and "version 8" to "version 9"
upgrade process.

## Start with v7 image:

### Host Machine

```
# Checkout the last v7 patch
git checkout 7940c1199697d26c59f6a6600a94054ea8098c5b
fx set bringup.x64 && fx build
./out/default.zircon/host-x64-linux-clang/obj/tools/minfs/minfs minfs.bin@64M mkfs
# Create an FVM containing the minfs partition.
./out/default.zircon/host-x64-linux-clang/obj/tools/fvm/fvm fvm.bin@64M create --default minfs.bin
# Checkout this patch
git checkout a-branch-with-this-patch
fx build && fx run -k -- -hda $PWD/minfs.bin
```

### Target Machine

```
mount -r /dev/class/block/000 /data # Expect failure; driver version mismatch.
fsck /dev/class/block/000 minfs     # Expect success (upgrade in logs)
fsck /dev/class/block/000 minfs     # Expect success.
mount /dev/class/block/000 /data    # Expect success
umount /data
```

### Host Machine (FVM)

```
fx run -k -- -hda $PWD/fvm.bin
```

### Target Machine (FVM)

```
mount -r /dev/class/block/001 /data # Expect failure; driver version mismatch.
mount /dev/class/block/000 /data    # Expect success (upgrade in logs)
umount /data
fsck /dev/class/block/000 minfs     # Expect success.
```

## Start with v8 image:

### Host Machine

```
# Checkout the last v8 patch
git checkout origin/master
fx set bringup.x64 && fx build
./out/default.zircon/host-x64-linux-clang/obj/tools/minfs/minfs minfs.bin@64M mkfs
# Create an FVM containing the minfs partition.
./out/default.zircon/host-x64-linux-clang/obj/tools/fvm/fvm fvm.bin@64M create --default minfs.bin
# Checkout this patch
git checkout a-branch-with-this-patch
fx build && fx run -k -- -hda $PWD/minfs.bin
```

### Target machine

```
fsck /dev/class/block/000 minfs    # Expect success.
mount /dev/class/block/000 /data   # Expect success (normal mount).
umount /data
mount -j /dev/class/block/000 data # Expect success (normal mount).
umount /data
fsck /dev/class/block/000 minfs    # Expect success.
```

### Host Machine (FVM)

```
fx run -k -- -hda $PWD/fvm.bin
```

### Target Machine (FVM)

```
mount -r /dev/class/block/001 /data # Expect failure; driver version mismatch.
mount /dev/class/block/000 /data    # Expect success (upgrade in logs)
umount /data
fsck /dev/class/block/000 minfs     # Expect success.
```

## Start with v9 image:

### Host Machine

```
# Checkout this patch
git checkout a-branch-with-this-patch
fx set bringup.x64 && fx build
truncate --size 64M minfs.bin
/out/default.zircon/host-x64-linux-clang/obj/tools/minfs/minfs minfs.bin@64M mkfs
fx build && fx run -k -- -hda $PWD/minfs.bin
```

### Target machine

```
mount /dev/class/block/000 /data   # Expect success (normal mount).
umount /data
fsck /dev/class/block/000 minfs    # Expect success.
```
