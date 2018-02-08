# Third-party Rust Crates


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

## Adding a new vendored dependency

If a crate is not available in the vendor directory, it needs to be added with
the following steps.

1. Reference the crates you need in your manifest file;
1. Run the commands listed above.

Linking to a native library is not currently supported.

## Adding a new mirror

1. Request the addition of a mirror on *fuchsia.googlesource.com*;
1. Add the mirror to the [Jiri manifest][jiri-manifest] for the Rust runtime;
1. Add a patch section for the crate to the workspace;
1. Run the update script.


[3p-crates]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/vendor "Third-party crates"
[source-replacement]: http://doc.crates.io/source-replacement.html "Source replacement"
[jiri-manifest]: https://fuchsia.googlesource.com/manifest/+/master/runtimes/rust "Jiri manifest"
