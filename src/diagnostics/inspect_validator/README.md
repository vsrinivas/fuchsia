# Inspect Validator

Reviewed on: 2019-08-05

Inspect Validator exercises libraries that write Inspect VMOs and evaluates
the resulting VMOs for correctness and memory efficiency.

## Building

This project can be added to builds by including `--with //src/diagnostics/inspect_validator:tests`
to the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with '//topaz/bundles:buildbot' --with //src/diagnostics/inspect_validator:tests
```

## Running

Inspect Validator will (eventually) be run as part of CQ/CI, and can be
run manually via:

```
fx run-test inspect_validator_tests
```

## Testing

```
fx run-test inspect_validator_tests
```
also runs unit tests.

## Source layout

The test entrypoint is located in `src/client.rs`. It connects to and controls
one or more "puppet" programs located under lib/(language) such as
lib/rust/src/main.rs.



