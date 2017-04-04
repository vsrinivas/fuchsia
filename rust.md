Rust
====

Note: this is very much a work in progress, stay tuned for updates!


## Targets

There are two gn targets for building Rust:
- [`rust_library`][target-library] defines a library which can be used by other
targets;
- [`rust_binary`][target-binary] defines an executable.

Note that both targets can be used with the Fuchsia toolchain and with the host
toolchain.


# Managing third-party dependencies

Third-party crates are stored in [`//third-party/rust-crates/vendor`][3p-crates]
which we use as a [directory source][source-replacement].

To be able to run the update script, you first need to install the
`cargo-vendor` utility:
its dependencies lack the proper licenses.
```
scripts/install_cargo_vendor.sh
```

To update these crates, run the following commands:
```
scripts/update_rust_packages.py
build/rust/fetch-vendor-license.py --directory third_party/rust-crates/vendor
```
The configurations used as a reference to generate the set of required crates
are listed in the [`update_rust_packages.py`][update-script] script.


[target-library]: https://fuchsia.googlesource.com/build/+/master/rust/rust_library.gni "Rust library"
[target-binary]: https://fuchsia.googlesource.com/build/+/master/rust/rust_binary.gni "Rust binary"
[3p-crates]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/vendor "Third-party crates"
[source-replacement]: http://doc.crates.io/source-replacement.html "Source replacement"
[update-script]: https://fuchsia.googlesource.com/scripts/+/master/update_rust_packages.py "Update script"
