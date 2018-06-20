# Rust

## Targets

There are two GN targets which should be used for Rust projects:
- [`rustc_library`][target-library-rustc] defines a library which can be used
  by other targets.
- [`rustc_binary`][target-binary-rustc] defines an executable.

These GN targets do not require a `Cargo.toml` file, as they are not built with
Cargo during a normal build of Fuchsia. However, a `Cargo.toml` file can be
created for these targets by running `fx gen-cargo --target path/to/target:label`.
In order to generate a Cargo.toml, there must have first been a successful build
of Fuchsia which included the target. Generating `Cargo.toml` files is useful
for integration with IDEs such as Intellij and VSCode, and for using the
[`fargo`][fargo] tool to build and test targets without going through a full
GN build and run cycle each time.

## Building With a Custom Toolchain

If you want to test out Fuchsia with your own custom-built versions of rustc or cargo,
you can set the `rustc_prefix` argument to `fx set`, like this:

```
fx set x64 --release --args "rustc_prefix=\"/path/to/bin/dir\""
```

## Going further

- [Managing third-party dependencies](third_party.md)
- [Unsafe code](unsafe.md)
- [Unstable features](unstable.md)


[target-library-rustc]: https://fuchsia.googlesource.com/build/+/master/rust/rustc_library.gni "Rust library"
[target-binary-rustc]: https://fuchsia.googlesource.com/build/+/master/rust/rustc_binary.gni "Rust binary"
[fargo]: https://fuchsia.googlesource.com/fargo
