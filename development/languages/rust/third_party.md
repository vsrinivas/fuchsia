# Third-party Rust Crates

Third-party crates depended on by `rustc_binary` and `rustc_library` targets
are stored in [`//third-party/rust-crates/rustc_deps/vendor`][].
This set of crates is based on the dependencies listed in
[`//third_party/rust-crates/rustc_deps/Cargo.toml][3p-cargo-toml],
and is updated by running `fx update-rustc-third-party`, which will update
the precise versions of the crates used in the `Cargo.lock` file and download
any necessary crates into the `vendor` dir.

## Adding a new vendored dependency

If a crate is not available in the vendor directory, it can to be added with
the following steps:

1. Reference the crates you need in [`rustc_deps/Cargo.toml`][3p-cargo-toml].
1. Run `fn update-rustc-third-party`.
1. Merge the change into [third_party/rust-crates][3p-crates].
1. Update the git revision of `third_party/rust-crates` in
   [garnet/manifest/third_party][3p-manifest]:

```
<project name="rust-crates"
         path="third_party/rust-crates"
         remote="https://fuchsia.googlesource.com/third_party/rust-crates"
         revision="<YOUR_NEW_REVISION_HERE>"
         gerrithost="https://fuchsia-review.googlesource.com"/>
```

Once all that is done, navigate to `third_party/rust-crates` locally,
run `git checkout <YOUR_NEW_REVISION_HERE>`, build Garnet to ensure that your
change is working, then open a CL.

Linking to a native library is not currently supported.

## Adding a new mirror

1. Request the addition of a mirror on *fuchsia.googlesource.com*;
1. Add the mirror to the [Jiri manifest][jiri-manifest] for the Rust runtime;
1. Add a patch section for the crate to the workspace;
1. Run the update script.

[3p-crates]: https://fuchsia.googlesource.com/third_party/rust-crates/
[3p-cargo-toml]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/rustc_deps/Cargo.toml
[3p-manifest]: https://fuchsia.googlesource.com/garnet/+/master/manifest/third_party#190
[3p-vendor]: https://fuchsia.googlesource.com/third_party/rust-crates/+/master/rustc_deps/vendor/
[jiri-manifest]: https://fuchsia.googlesource.com/manifest/+/master/runtimes/rust "Jiri manifest"
