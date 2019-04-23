# Bootserver

The `bootserver` host tool can be used to pave, netboot or boot Fuchsia on a
target device. This tool is very likely to go away in the short future with
a replacement being currently implemented.

## x64

### Generic

To pave and boot on a generic `x64` target, run:

```
bootserver \
    --boot "$IMAGES_PATH/fuchsia.zbi" \
    --bootloader "$IMAGES_PATH/fuchsia.esp.blk" \
    --fvm "$IMAGES_PATH/obj/build/images/fvm.sparse.blk" \
    --zircona "$IMAGES_PATH/fuchsia.zbi" \
    --zirconr "$IMAGES_PATH/zedboot.zbi"
```

### Chromebook

To pave and boot on a `chromebook` target, run:


```
bootserver \
    --boot "$IMAGES_PATH/fuchsia.zbi" \
    --fvm "$IMAGES_PATH/obj/build/images/fvm.sparse.blk" \
    --zircona "$IMAGES_PATH/fuchsia.zbi.vboot" \
    --zirconr "$IMAGES_PATH/zedboot.vboot"
```


## arm64

To pave and boot on an `arm64` target, run:

```
bootserver \
    --boot "$IMAGES_PATH/fuchsia.zbi" \
    --fvm "$IMAGES_PATH/obj/build/images/fvm.sparse.blk" \
    --zircona "$IMAGES_PATH/fuchsia.zbi" \
    --zirconr "$IMAGES_PATH/zedboot.zbi"
```
