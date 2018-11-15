# Bootserver

The `bootserver` host tool can be used to pave, netboot or boot Fuchsia on a
target device. This tool is very likely to go away in the short future with
a replacement being currently implemented.


## x64

To pave and boot on an `x64` target, run:

```
bootserver --efi "<IMAGE_PATH>/local.esp.blk" --fvm "<IMAGE_PATH>/fvm.sparse.blk" --kernc "<IMAGE_PATH>/zircon.vboot" --zircona "<IMAGE_PATH>/fuchsia.zbi" "<IMAGE_PATH>/fuchsia.zbi" -1
```

## arm64

To pave and boot on an `arm64` target, run:

```
bootserver --fvm "<IMAGE_PATH>/obj/build/images/fvm.sparse.blk" --zircona "<IMAGE_PATH>/fuchsia.zbi" --zirconb "<IMAGE_PATH>/fuchsia.zbi" -1
```
