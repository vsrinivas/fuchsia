# Generate C bindings

## Install cbindgen

We are using `cbindgen 0.8.0` to auto generate our C bindings.
Install the tool via:
```
cargo install cbindgen
```

## Generate Cargo.toml file

Generate a Cargo.toml for `wlan-mlme-c` by running the following command:
```
cd $FUCHSIA_DIR/garnet/lib/rust/wlan-mlme-c
rm Cargo.lock Cargo.toml
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

* Error message
```
cbindgen: error while loading shared libraries: libproc_macro-7924135655fecacc.so: cannot open shared object file: No such file or directory
```
Make sure rustc is the latest version by checking `rustc --version`. `rustc 1.34.0-nightly (00aae71f5 2019-02-25)` is known to work with `cbindgen 0.8.0`. If they become out of sync in some cases, try the latest version that is usually within the same day by running:
```
rustup update
rustup default nightly
```