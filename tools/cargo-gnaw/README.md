# Cargo GNaw
**Tooling to convert Cargo.toml files into native GN rules**

[![crates.io](https://img.shields.io/crates/v/cargo-gnaw.svg)](https://crates.io/crates/cargo-gnaw)
[![license](https://img.shields.io/badge/license-BSD3.0-blue.svg)](https://github.com/google/cargo-gnaw/LICENSE)
[![docs.rs](https://docs.rs/com/badge.svg)](https://docs.rs/crate/cargo-gnaw/)
![cargo-gnaw](https://github.com/google/cargo-gnaw/workflows/cargo-gnaw/badge.svg)

## Install
```sh
$ cargo install --path ~/cargo-gnaw
```

## Run
```sh
$ cargo gnaw --manifest-path ~/fuchsia/third_party/rust_crates/Cargo.toml -o ~/fuchsia/third_party/rust_crates/BUILD.gn
```

### Options
* --skip-root - Skip the root package in the Cargo.toml and treat it's dependencies as the top-level targets
* --gn-bin - Path to GN binary for formatting the output

## How it works

Cargo GNaw operates on vendored crates to convert them into [GN](https://gn.googlesource.com/gn/+/master/docs/reference.md) rules. The resulting BUILD.gn file is expected
to be vendored with the crates and provides targets for the GN build system to reference.

All top-level crates are given an easy to use GN alias group that references the version exposed in the Cargo.toml.
Direct dependencies of the root crate can be "lifted" to the top-level by skipping the default root crate.


#### Build Scripts
GNaw intentionally does not handle build.rs scripts at compilation time. Any evaluation of a build.rs script is done when the crate is vendored.
The resulting configuration which is usually produced by the build.rs script is put into a section in the source Cargo.toml. Options detailed below.
Simple build.rs scripts (ones that only depend upon Rust's `std` library) evaluate and automatically provide the author with the expected configuration.


### GN configs
Underneath a TOML array, a configuration should be passed as "gn.crate.<Name>.<ExactVersion>"

* `rustflags` - flags to pass through to rustc
* `deps` - native GN dependency
* `env_vars` - environment variables, usually used for pretending to be cargo
* `platform` - platform this configuration targets. Uses the rust cfg format (Ex: cfg(unix))

Example:
```toml
[[gn.crate.anyhow."1.0.25"]]
rustflags = ["--cfg=backtrace"]
```



## Simple Example
```toml
[package]
name = "simple"
version = "1.0.25"
authors = ["Benjamin Brittain <bwb@google.com>"]
edition = "2018"

[dependencies]
```

converts to

```gn

group("simple") {
  deps = [":simple-1-0-25"]
}

rust_library("simple-1-0-25") {
  crate_name = "simple"
  crate_root = "//tools/cargo-gnaw/src/tests/simple/src/lib.rs"
  output_name = "simple-9ac42213326ac72d"

  deps = []

  rustenv = []
  rustflags = ["--cap-lints=allow",
               "--edition=2018",
               "-Cmetadata=9ac42213326ac72d",
               "-Cextra-filename=9ac42213326ac72d"]
}

```
