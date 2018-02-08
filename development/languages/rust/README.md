# Rust


## Targets

There are two gn targets for building Rust:
- [`rust_library`][target-library] defines a library which can be used by other
targets;
- [`rust_binary`][target-binary] defines an executable.

Note that both targets can be used with the Fuchsia toolchain and with the host
toolchain.

These GN targets must be complemented by a
[`Cargo.toml` manifest file][manifest] just like that of a regular Rust
crate.

## Workspace

`garnet/Cargo.toml` establishes a workspace for all the crates in the garnet
subtree of Fuchsia. All crates in garnet that are part of the build must be listed
in the members array of the workspace. All crates in Garnet that appear in the
dependencies section of any crate in Garnet should have a matching patch
statement in the workspace file. Refer to the workspace file for examples.

When adding a new crate to Garnet it is important to add it to both sections
of the workspace file.

## FIDL Facade Crates

A Cargo workspace requires that all crates in the workspace live in the same file
system subtree as the workspace file. Fuchsia build system rules do not allow generated
files to live in the source tree. To resolve this conflict there are facade crates
in the garnet tree that locate the generated code at compile time and include it with
`include!()`. See `garnet/public/rust/fidl_crates/garnet_examples_fidl_services` for
an example.

## Non-Rust Dependencies

If a Garnet Rust crate depends on something that cannot be expressed in Cargo it
must specify that dependency in BUILD.gn. The most common case of this is when
a Rust crate depends on one of the FIDL facade crates.

This is true for transitive dependencies, so if crate A depends on crate B which
depends on a non-Rust dependency, there must be a gn dependency between A and B as
well as B and C.

Here's an example of a library depending on a FIDL library
at `//garnet/examples/fidl/services`:

```
BUILD.gn
--------
import("//build/rust/rust_library.gni")

rust_library("garnet_examples_fidl_services") {
  deps = [
    "//garnet/examples/fidl/services:services_rust",
  ]
}

Cargo.toml
----------
[package]
name = "garnet_examples_fidl_services"
version = "0.1.0"
license = "BSD-3-Clause"
authors = ["Rob Tsuk <robtsuk@google.com>"]
description = "Generated interface"
repository = "https://fuchsia.googlesource.com/garnet/"

[dependencies]
fidl = "0.1.0"
fuchsia-zircon = "0.3"
futures = "0.1.15"
tokio-core = "0.1"
tokio-fuchsia = "0.1.0"
```

## Testing

Both `rust_library` and `rust_binary` have a `with_tests` attribute which, if
set to _true_, will trigger unit tests associated with the target to be built
alongside the target and packaged in a test executable.

Integration tests are currently not supported.


## GN build strategy

Integration of Rust into build systems is still very much
[a work in progress][build-integration]. It largely boils down to the decision
of using the `rustc` compiler and handling dependencies directly in the build
system, or letting the high-level [Cargo][cargo] package manager take care of
most of the work. Here's a quick breakdown of how the two strategies compare:

| Approach                                       | rustc        | cargo                                       |
|------------------------------------------------|--------------|---------------------------------------------|
| Integration cost                               | High         | Low                                         |
| Redundancy of build steps                      | Low          | Medium                                      |
| Handling of third-party deps                   | Fully manual | Automated with [cargo vendor][cargo-vendor] |
| Granularity of third-party deps integration    | High         | Low                                         |
| Handling of generated code (e.g. [FIDL][fidl]) | Fully manual | Fully manual                                |
| Proximity to standard workflow                 | Low          | High                                        |

Given the relatively low amount of Rust code we currently host in the Fuchsia
source tree, the Cargo-based approach made more sense:
- extra build costs remain low overall;
- maintenance of the build system is straightforward;
- familiar workflow for existing Rust developers.


## Building With a Custom Toolchain

If you want to test out Fuchsia with your own custom-built versions of rustc or cargo,
you can set the `rustc_prefix` argument to `fx set`, like this:

```
fx set x86-64 --release --args "rustc_prefix=\"/path/to/bin/dir\""
```

## Going further

- [Managing third-party dependencies](third_party.md)
- [Unsafe code](unsafe.md)


[target-library]: https://fuchsia.googlesource.com/build/+/master/rust/rust_library.gni "Rust library"
[target-binary]: https://fuchsia.googlesource.com/build/+/master/rust/rust_binary.gni "Rust binary"
[manifest]: http://doc.crates.io/manifest.html "Manifest file"
[build-integration]: https://github.com/rust-lang/rust-roadmap/issues/12 "Build integration"
[cargo]: https://github.com/rust-lang/cargo "Cargo"
[cargo-vendor]: https://github.com/alexcrichton/cargo-vendor "cargo-vendor"
[fidl]: https://fuchsia.googlesource.com/fidl/+/master/README.md "FIDL"
