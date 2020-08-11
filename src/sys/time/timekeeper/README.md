# timekeeper

timekeeper implements the [`fuchsia.time.Utc`][utc-fidl] protocol.

Because timekeeper is implemented in Rust, we recommend that you have consulted the [Fuchsia docs on
developing with Rust](fuchsia-rust-docs).

[fuchsia-rust-docs]: ../../../docs/development/languages/rust/README.md
[utc-fidl]: ./fidl/utc.fidl

## Getting Started

Generate a Cargo.toml for your editor to use:

```
fx build //src/sys/timekeeper:bin_cargo
fx gen-cargo //src/sys/timekeeper:bin
```

### Documentation

`fx rustdoc src/sys/timekeeper:bin --open`

### Building

`timekeeper` itself is included in the `core` product configuration, no specific `fx set` is needed
to ensure it is included in an image. You may wish to build *only* a small image like core's while
working on this service. Our tests must be included explicitly in your device's package universe:

`fx set PRODUCT.ARCH --with //src/sys/timekeeper:tests`

After this, `fx build` will include the test package as well.

### Running tests

Once you have your build working:

`fx shell killall timekeeper_bin_test.cmx ; fx run-test timekeeper_bin_test`

This command ensures that any previous instances of the test have been exited before running again.
Because deadlocks (and the need to exit them with ctrl+c) are common when writing event
notification tests, dead test instances can stack up quickly during a development session.

### Formatting

Minimum before submitting a CL: `fx rustfmt //src/sys/timekeeper:bin`. Prefer `fx format-code`.

## Concepts

### Minimum UTC ("backstop" time)

Devices store a minimum UTC value at `/config/build-info/minimum-utc-stamp`, which is generated at
build time and included in the system image. It is stored as a Unix epoch (seconds since 1970,
excluding leap seconds) and can be quickly observed on your current device. An example from the
time of writing:

```
$ [fx shell --] cat /config/build-info/minimum-utc-stamp | date
Wed 17 Jul 2019 03:56:35 PM PDT
```

All reads of the UTC clock on a device must return a value at or after the time stored in the file.
