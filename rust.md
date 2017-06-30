Rust
====

## Targets

There are two gn targets for building Rust:
- [`rust_library`][target-library] defines a library which can be used by other
targets;
- [`rust_binary`][target-binary] defines an executable.

Note that both targets can be used with the Fuchsia toolchain and with the host
toolchain.

These GN targets should be complemented by a
[`Cargo.toml` manifest file][manifest] very similar to that of a regular Rust
crate except for how dependencies are handled.

### Dependencies

There are two types of dependencies:
- in-tree dependencies (e.g. FIDL, other rust_library targets);
- third-party dependencies.

Third-party dependencies should be added to the manifest file just like in the
normal Rust development workflow - see the next section for how third-party
dependencies are inserted into the build.
All dependencies should be added to the build file as target dependencies.

Here's an example of a library depending on the third-party crate `bitflags`
and on a FIDL library at `//apps/framework/services`:
```
BUILD.gn
--------
rust_library("my-library") {
  deps = [
    "//apps/framework/services:services_rust_library",
    "//third_party/rust-crates:bitflags-0.7.0",
  ]
}

Cargo.toml
----------
[package]
name = "my-library"
version = "0.1.0"

[dependencies]
bitflags = "0.7.0"
```

## Testing

Both `rust_library` and `rust_binary` have a `with_tests` attribute which, if
set to _true_, will trigger unit tests associated with the target to be built
alongside the target and packaged in a test executable.

Integration tests are currently not supported.


## Managing third-party dependencies

Third-party crates are stored in [`//third-party/rust-crates/vendor`][3p-crates]
which we use as a [directory source][source-replacement] automatically set up
by the build system.

To be able to run the update script, you first need to build the
`cargo-vendor` utility:
```
scripts/build_cargo_vendor.sh
```

To update these crates, run the following command:
```
scripts/update_rust_crates.py --cargo-vendor third_party/rust-crates/manual/cargo-vendor/target/debug/cargo-vendor
```

The configurations used as a reference to generate the set of required crates
are listed in the [`update_rust_crates.py`][update-script] script.

### Adding a new third-party dependency

If a crate is not available in the vendor directory, it needs to be added with the following steps.

1. Create your crate's manifest file and reference the crates you need;
1. Add your crate root to `CONFIGS` in the [update script][update-script];
1. Run the commands listed above.

If a crate needs to link against a native library, the library needs to be
present in the `NATIVE_LIBS` parameter in the [update script][update-script].


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

Note that while Cargo knows how to use the vendor directory to build a single
crate, some GN glue had to be added in order to properly integrate with native
libraries built independently from the crate.


[target-library]: https://fuchsia.googlesource.com/build/+/master/rust/rust_library.gni "Rust library"
[target-binary]: https://fuchsia.googlesource.com/build/+/master/rust/rust_binary.gni "Rust binary"
[manifest]: http://doc.crates.io/manifest.html "Manifest file"
[3p-crates]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/vendor "Third-party crates"
[source-replacement]: http://doc.crates.io/source-replacement.html "Source replacement"
[update-script]: https://fuchsia.googlesource.com/scripts/+/master/update_rust_crates.py "Update script"
[build-integration]: https://github.com/rust-lang/rust-roadmap/issues/12 "Build integration"
[cargo]: https://github.com/rust-lang/cargo "Cargo"
[cargo-vendor]: https://github.com/alexcrichton/cargo-vendor "cargo-vendor"
[fidl]: https://fuchsia.googlesource.com/fidl/+/master/README.md "FIDL"
