# Generate C bindings

## Install cbindgen

We are using `cbindgen` to auto generate our C bindings.
Install the tool via:
```
cargo install cbindgen
```

## Command line

```
cbindgen $FUCHSIA_DIR/garnet/lib/rust/wlan-mlme/ -o $FUCHSIA_DIR/garnet/lib/rust/wlan-mlme/abi.h
```

After re-generating the bindings also run `fx format-code` to format the generated bindings.