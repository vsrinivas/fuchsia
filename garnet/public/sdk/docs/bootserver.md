# Bootserver

The `bootserver` host tool can be used to pave, netboot or boot Fuchsia on a
target device. This tool is very likely to go away in the short future with
a replacement being currently implemented.

## x64

To pave and boot on an `x64` target, run:

```
bootserver --efi "$IMAGES_PATH/local.esp.blk" --fvm "$IMAGES_PATH/fvm.sparse.blk" --kernc "$IMAGES_PATH/zircon.vboot" --zircona "$IMAGES_PATH/fuchsia.zbi" "$IMAGES_PATH/fuchsia.zbi" -1
```

## arm64

To pave and boot on an `arm64` target, run:

```
bootserver --fvm "$IMAGES_PATH/obj/build/images/fvm.sparse.blk" --zircona "$IMAGES_PATH/fuchsia.zbi" --zirconb "$IMAGES_PATH/fuchsia.zbi" -1
```
