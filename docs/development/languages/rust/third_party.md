# Third-party Rust Crates

## Overview
OSRB approval is required for third-party crates. To get approval, you will
need to follow the instructions under the "Process for 3rd Party Hosted
Code" section in [this document][osrb-process].

Third-party crates depended on by `rustc_binary` and `rustc_library` targets
are stored in [`//third-party/rust-crates/rustc_deps/vendor`][3p-vendor].
This set of crates is based on the dependencies listed in
[`//third_party/rust-crates/rustc_deps/Cargo.toml`][3p-cargo-toml],
and is updated by running `fx update-rustc-third-party`, which will update
the precise versions of the crates used in the `Cargo.lock` file and download
any necessary crates into the `vendor` dir.

## Adding a new vendored dependency

If a crate is not available in the vendor directory, it can to be added with
the following steps:

1. Reference the crates you need in [`rustc_deps/Cargo.toml`][3p-cargo-toml].
1. Run `scripts/fx update-rustc-third-party`. This will download all crates listed in
   [`rustc_deps/Cargo.toml`][3p-cargo-toml] as well as their dependencies and
   place them in the `vendor` directory.
1. `git add` the `Cargo.toml`, `Cargo.lock` and `vendor` directory.
1.  __Do not__  upload this change to gerrit yet.
1. Get OSRB approval. Make sure to include the requested information for all
   new crates pulled in by your new dependency.
   If there are any files in the source repository that are not included when
   vendored, make a note of that for the OSRB reviewer. For example, font files
   that are only used for testing but are excluded when the crate is vendored.
   If you are not a Google employee, you will need to ask a Google employee to
   do this part for you.
1. After getting the OSRB approval, upload the change to Gerrit.
Get code-review+2, merge the change into [third_party/rust-crates][3p-crates].
1. Auto-rollers will update the git revision.
If you need to update the revision manually, update the git revision
in `integration`. Update the git revision of `third_party/rust-crates` in
   [integration/garnet/third_party][3p-manifest]:

```
<project name="rust-crates"
         path="third_party/rust-crates"
         remote="https://fuchsia.googlesource.com/third_party/rust-crates"
         revision="<YOUR_NEW_REVISION_HERE>"
         gerrithost="https://fuchsia-review.googlesource.com"/>
```

Linking to a native library is not currently supported.

## Adding a new mirror

1. Request the addition of a mirror on *fuchsia.googlesource.com*;
1. Add the mirror to the [Jiri manifest][jiri-manifest] for the Rust runtime;
1. Add a patch section for the crate to the workspace;
1. Run the update script.

[3p-crates]: https://fuchsia.googlesource.com/third_party/rust-crates/
[3p-cargo-toml]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/rustc_deps/Cargo.toml
[3p-manifest]: https://fuchsia.googlesource.com/integration/+/master/garnet/third_party
[3p-vendor]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/rustc_deps/vendor/
[osrb-process]: https://docs.google.com/document/d/1X3eNvc4keQxOpbkGUiyYBMtr3ueEnVQCPW61FT96o_E/edit#heading=h.7mb7m2qs89th
[jiri-manifest]: https://fuchsia.googlesource.com/manifest/+/master/runtimes/rust "Jiri manifest"
