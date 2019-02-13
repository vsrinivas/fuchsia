# Generate C bindings

## Install cbindgen

We are using `cbindgen` to auto generate our C bindings.
Install the tool via:
```
cargo install cbindgen
```

## Generate Cargo.toml files

Generate `wlan-common`, `wlan-mlme` and `wlan-mlme-c` Cargo.toml files by running the following command from each directory:
```
fx gen-cargo .
```

## Command line

```
cbindgen $FUCHSIA_DIR/garnet/lib/rust/wlan-mlme-c/ -o $FUCHSIA_DIR/garnet/lib/rust/wlan-mlme-c/bindings.h
```

After re-generating the bindings also run `fx format-code` to format the generated bindings.

## Troubleshooting

* `cbindgen` fails when running `cargo metadata`:
 Remove the Cargo.toml and Cargo.lock files from `wlan-mlme-c` folder and re-run `fx gen-cargo .`.

* ERROR: Parsing crate `wlan_mlme_c`: can't find dependency version for `xyz`:
Rebuild Fuchsia and re-run `fx gen-cargo .`

* A binding is not generated:
Run `cbindgen` with `-v` argument to get a better understand what types and functions were found and if they've been rejected.