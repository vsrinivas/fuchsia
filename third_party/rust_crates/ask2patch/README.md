# Crates which require permission to patch upstream

Crates in this directory don't fall under Google's [patching policy]. Googlers who wish to
contribute to the upstream projects here must seek approval by filing a bug under the `OSRB`
component.

Crates moved to this directory should not be modified from their upstream contents. If you need to
make code changes, move the crate to `//third_party/rust_crates/forks/<crate_name>` and add a
`README.fuchsia` file explaining the changes *and* this upstream patching policy restriction.

[patching policy]: https://opensource.google/docs/patching/
