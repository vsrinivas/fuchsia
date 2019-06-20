# netclock

netclock implements the [`fuchsia.time.Utc`][utc-fidl] protocol.

Because netclock is implemented in Rust, we recommend that you have consulted the [Fuchsia docs on
developing with Rust](fuchsia-rust-docs).

[fuchsia-rust-docs]: ../../../docs/development/languages/rust/README.md
[utc-fidl]: ./fidl/utc.fidl

## Getting Started

Generate a Cargo.toml for your editor to use:

`fx gen-cargo //src/sys/netclock:bin`

### Documentation

`fx rustdoc src/sys/netclock:bin --open`

### Building

`netclock` itself is included in the `core` product configuration, no specific `fx set` is needed
to ensure it is included in an image. You may wish to build *only* a small image like core's while
working on this service. Our tests must be included explicitly in your device's package universe:

`fx set PRODUCT.ARCH --with //src/sys/netclock:tests`

After this, `fx build` will include the test package as well.

### Running tests

Once you have your build working:

`fx shell killall netclock_bin_test.cmx ; fx run-test netclock_bin_test`

This command ensures that any previous instances of the test have been exited before running again.
Because deadlocks (and the need to exit them with ctrl+c) are common when writing event
notification tests, dead test instances can stack up quickly during a development session.

### Formatting

Minimum before submitting a CL: `fx rustfmt //src/sys/netclock:bin`. Prefer `fx format-code`.
