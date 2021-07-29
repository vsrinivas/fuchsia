# Crates which require permission to patch upstream

Crates in this directory don't fall under Google's [patching policy]. Googlers who wish to
contribute to the upstream projects here must seek approval by filing a bug under the `OSRB`
component.

Crates moved to this directory should not be modified from their upstream contents. If you need to
make code changes, move the crate to `//third_party/rust_crates/forks/<crate_name>` and add a
`README.fuchsia` file explaining the changes *and* this upstream patching policy restriction.

To update a crate in this directory:

1. Comment out its line in the `[patch.crates-io]` section of `//third_party/rust_crates/Cargo.toml`.
2. Run `fx update-rustc-third-party`.
3. Delete all of `//third_party/rust_crates/ask2patch/<crate_name>` except the `OWNERS` file.
4. Move the contents of `//third_party/rust_crates/vendor/<crate_name>/*` to
   `//third_party/rust_crates/ask2patch/<crate_name>/*` and delete the directory in `vendor/`.
5. Undo (1), uncommenting the crate's line in `//third_party/rust_crates/Cargo.toml`.
6. Repeat (2).

[patching policy]: https://opensource.google/docs/patching/
