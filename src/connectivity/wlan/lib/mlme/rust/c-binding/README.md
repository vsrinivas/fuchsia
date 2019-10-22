# Generate C bindings

## Install cbindgen

We are using `cbindgen 0.9.1` to auto generate our C bindings.
Install the tool via:
```
cargo install --force cbindgen
```

## Generate Cargo.toml file

Generate a Cargo.toml for `wlan-mlme-c` by running the following commands:
```
find $FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/ -name 'Cargo.*' | xargs rm -f
fx build src/connectivity/wlan/lib/mlme/rust/c-binding:_wlan-mlme-c_rustc_artifact_cargo
fx gen-cargo //src/connectivity/wlan/lib/mlme/rust/c-binding:_wlan-mlme-c_rustc_artifact
```

## Command line

```
cbindgen $FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/ -o $FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h
fx format-code --files=$FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h
```

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
Make sure rustc is the latest version by checking `rustc --version`. `rustc 1.39.0-nightly (2b8116dce 2019-09-08)` is known to work with `cbindgen 0.9.1`. If they become out of sync in some cases, try the latest version that is usually within the same day by running:
```
rustup update
rustup default nightly
```
