# Generate C bindings

## Install cbindgen

We are using `cbindgen 0.15.0` to auto generate our C bindings.
Install the tool via:
```
cargo install --version 0.15.0 --force cbindgen
```

## Generate Cargo.toml file

Generate a Cargo.toml for `wlan-mlme-c` by following [the instructions on fuchsia.dev](https://fuchsia.dev/fuchsia-src/development/languages/rust/cargo)
for the build target `//src/connectivity/wlan/lib/mlme/rust/c-binding:wlan-mlme-c`.
As of this writing, this is done in the following way.

```
fx set PRODUCT.BOARD --cargo-toml-gen <other fx args>
fx build build/rust:cargo_toml_gen
fx gen-cargo //src/connectivity/wlan/lib/mlme/rust/c-binding:_wlan_mlme_c_rustc_static
```

## Create the C bindings

```
cbindgen $FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/ -o $FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h
```

## Format the bindings

At present, cbindgen changes the comment on the final line to use a `/* */` style
comment for the header guard. This is not recognized by `fx format-code`. Change
the final line of `bindings.h` to be the following.

```
#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_RUST_C_BINDING_BINDINGS_H_
```

And finally, run `fx format-code` and check-in the new `bindings.h`

```
fx format-code --files=$FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h
git add $FUCHSIA_DIR/src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h
```

## Troubleshooting

* `cbindgen` fails when running `cargo metadata`: Remove the Cargo.toml and Cargo.lock
files from `wlan-mlme-c` folder and re-run `fx gen-cargo .`.

* ERROR: Parsing crate `wlan_mlme_c`: can't find dependency version for `xyz`:
Rebuild Fuchsia and re-run `fx gen-cargo .`

* A binding is not generated: Run `cbindgen` with `-v` argument to get a better understand
what types and functions were found and if they've been rejected.

* Error message

```
cbindgen: error while loading shared libraries: libproc_macro-7924135655fecacc.so: cannot open shared object file: No such file or directory
```

Make sure rustc is the latest version by checking `rustc --version`. `rustc 1.47.0-nightly
(e5e33ebd2 2020-08-11)` is known to work with `cbindgen 0.15.0`. If they become out
of sync in some cases, try the latest version that is usually within the same day
by running:

```
rustup update
rustup default nightly
```
