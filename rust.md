Rust
====

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

## Managing third-party dependencies

Third-party crates are stored in [`//third-party/rust-crates/vendor`][3p-crates]
which we use as a [directory source][source-replacement] automatically set up
by the build system.

In addition, we maintain some local mirrors of projects with Fuchsia-specific
changes which haven't made it to *crates.io*. These mirrors are located in
`//third_party/rust-mirrors`.

To be able to run the script updating third-party crates, you first need to
build the `cargo-vendor` utility:
```
scripts/rust/build_cargo_vendor.sh
```

To update these crates, run the following command:
```
build/rust/update_rust_crates.sh
```

This script will update the `third_party/rust-crates` repository, and will
update `garnet/Cargo.lock` to include any new or updated dependencies.

If you are adding a third-party dependency to an existing crate or creating
a new crate, you'll need to either run `update_rust_crates.sh` or temporarily
pass use_frozen_with_cargo=false to gn. If you are using the fx scripts,
you can do so as follows:

    ./scripts/fx set x86 --release --packages garnet/packages/default --args "use_frozen_with_cargo=false"

Running `update_rust_crates.sh` to solve this problem will have the side effect
of updating all the existing crates in `third_party/rust-crates`, which may not be desired.
If you want to avoid this, set `use_frozen_with_cargo` to false and build once to update
`garnet/Cargo.lock`. When you're done adding dependencies, set `use_frozen_with_cargo` back to true.

The approach of avoiding running `update_rust_crates.sh` will only work if you're adding a
dependency on a crate that is already depended on by another crate in the tree, and thus already
appears in the lock file. If the new dependency is not already in the lock file you must run
`update_rust_crates.sh`.

### Adding a new vendored dependency

If a crate is not available in the vendor directory, it needs to be added with
the following steps.

1. Reference the crates you need in your manifest file;
1. Run the commands listed above.

Linking to a native library is not currently supported.

### Adding a new mirror

1. Request the addition of a mirror on *fuchsia.googlesource.com*;
1. Add the mirror to the [Jiri manifest][jiri-manifest] for the Rust runtime;
1. Add a patch section for the crate to the workspace;
1. Run the update script.

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

## Unsafe Code in Rust

`unsafe` is a dangerous but sometimes necessary escape hatch in Rust.
When writing or reviewing `unsafe` code, it's essential that you:
- clearly identify all of the assumptions and invariants required by every
  `unsafe` block;
- ensure that those assumptions are met;
- ensure that those assumptions will *continue* to be met.

In order to ensure that `unsafe` invariants are not broken by future editors,
each usage of `unsafe` must be accompanied by a clear, concise comment
explaining what assumptions are being made.

Where possible, package up unsafety into a single function or module which
provides a safe abstraction to the outside world. FFI calls should usually
be exposed through a safe function whose only purpose is to provide a safe
wrapper around the function in question. These functions should contain
a comment with the following information (if applicable):
- Preconditions (e.g. what are the valid states of the arguments?)
- Failure handling (e.g. what values should be free'd? forgotten? invalidated?)
- Success handling (e.g. what values are created or consumed?)

Example:

```rust
impl Channel {
    /// Write a message to a channel. Wraps the
    /// [zx_channel_write](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/channel_write.md)
    /// syscall.
    pub fn write(&self, bytes: &[u8], handles: &mut Vec<Handle>)
            -> Result<(), Status>
    {
        let opts = 0;
        let n_bytes = try!(usize_into_u32(bytes.len()).map_err(|_| Status::OUT_OF_RANGE));
        let n_handles = try!(usize_into_u32(handles.len()).map_err(|_| Status::OUT_OF_RANGE));

        // Requires that `self` contains a currently valid handle or ZX_HANDLE_INVALID.
        // On success, all of the handles in the handles array have been moved.
        // They must be forgotten and not dropped.
        // On error, all handles are still owned by the current process and can be dropped.
        unsafe {
            let status = sys::zx_channel_write(self.raw_handle(), opts, bytes.as_ptr(), n_bytes,
                handles.as_ptr() as *const sys::zx_handle_t, n_handles);
            ok(status)?;
            // Handles were successfully transferred, forget them on sender side
            handles.set_len(0);
            Ok(())
        }
    }
}
```

If `unsafe` code relies on other safe code for correctness, a comment
must be left alongside the corresponding safe code indicating what invariants
it must uphold and why. Invariants that rely upon the behavior of multiple
functions will draw extra scrutiny, and cross-module or cross-crate unsafety
requires even more attention. `unsafe` code that depends on correct behavior of
a third-party crate will likely be rejected, and `unsafe` code that depends
upon the internal representation details of third-party types will _never_ be
accepted.

Finally, `struct` definitions containing `unsafe` types such as `*const`,
`*mut`, or `UnsafeCell` must include a comment explaining the internal
representation invariants of the type. If the `unsafe` type is used to perform
a mutation OR if it aliases with memory inside another type, there should be
an explanation of how it upholds Rust's "aliasing XOR mutation" requirements.
If any `derive`able traits are purposefully omitted for safety reasons, a
comment must be left to prevent future editors from adding the unsafe impls.

The rules above are applied to any additions of `unsafe` code or any
modifications of existing `unsafe` code.

For more discussion on encapsulating `unsafe` invariants, see
[Ralf Jung's "The Scope of Unsafe"][scope-of-unsafe] and
[Niko Matsakis's "Tootsie Pop" model][tootsie-pop].

[target-library]: https://fuchsia.googlesource.com/build/+/master/rust/rust_library.gni "Rust library"
[target-binary]: https://fuchsia.googlesource.com/build/+/master/rust/rust_binary.gni "Rust binary"
[manifest]: http://doc.crates.io/manifest.html "Manifest file"
[3p-crates]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/vendor "Third-party crates"
[source-replacement]: http://doc.crates.io/source-replacement.html "Source replacement"
[update-script]: https://fuchsia.googlesource.com/scripts/+/master/rust/update_rust_crates.py "Update script"
[jiri-manifest]: https://fuchsia.googlesource.com/manifest/+/master/runtimes/rust "Jiri manifest"
[build-integration]: https://github.com/rust-lang/rust-roadmap/issues/12 "Build integration"
[cargo]: https://github.com/rust-lang/cargo "Cargo"
[cargo-vendor]: https://github.com/alexcrichton/cargo-vendor "cargo-vendor"
[fidl]: https://fuchsia.googlesource.com/fidl/+/master/README.md "FIDL"
[scope-of-unsafe]: https://www.ralfj.de/blog/2016/01/09/the-scope-of-unsafe.html
[tootsie-pop]: http://smallcultfollowing.com/babysteps/blog/2016/05/27/the-tootsie-pop-model-for-unsafe-code
