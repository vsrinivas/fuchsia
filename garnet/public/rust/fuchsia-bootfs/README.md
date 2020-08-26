Rust implementation of //src/lib/bootfs
==============================================================

This repository contains generated bindings for a portion of the Zircon bootfs library and bootfs.h
headers to allow Rust programs to parse bootfs payloads.

## Testing

To generate a test ZBI:
```sh
cd ${FUCHSIA_DIR}/garnet/public/rust/fuchsia-bootfs

# Generate an uncompressed test ZBI.
${FUCHSIA_OUT_DIR}/default.zircon/tools/zbi -u --output testdata/basic.bootfs.full testdata/input

# We don't want the ZBI item headers so remove them.
dd if=testdata/basic.bootfs.full of=testdata/basic.bootfs.uncompresssed bs=1 skip=64
rm testdata/basic.bootfs.full
```
