# zstd-sys tiny mirror

This library exists to support the inclusion of the zstd-rs library (github.com/gyscos/zstd-rs), which provides rust bindings for zstd. zstd-rs includes another library, zstd-sys. zstd-sys includes a copy of zstd along with the bindgen-generated bindings required for the library to function as a means of pinning the zstd version for compatibility.

To avoid including another copy of zstd in Fuchsia, this tiny mirror exists to provide bindings generated against Fuchsia's copy of zstd.

# Updating this library

If zstd or zstd-rs is updated in Fuchsia, there is a possibility that this library will become incompatible, though unlikely since it doesn't currently use experimental APIs.

To update this library, in addition to the steps required for updating a third-party crate at https://fuchsia.dev/fuchsia-src/development/languages/rust/third_party, the following should be done:

1. Update version in Cargo.toml to match what zstd-safe expects
2. Run bindgen.sh to generate new bindings. TODO(73858): automate this
3. Ideally test with a small rust project that incorporates zstd

TODO(73929): Add crate tests to CQ