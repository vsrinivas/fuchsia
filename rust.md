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


[target-library]: https://fuchsia.googlesource.com/build/+/master/rust/rust_library.gni "Rust library"
[target-binary]: https://fuchsia.googlesource.com/build/+/master/rust/rust_binary.gni "Rust binary"
