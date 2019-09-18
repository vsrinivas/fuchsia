# Inspect Validator

Reviewed on: 2019-09-15

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

Inspect Validator will be run as part of CQ/CI. To run manually, see "Testing".


## Testing
To run unit tests:
```
--with //src/diagnostics/inspect_validator:tests
fx run-test inspect_validator_tests
```
```
fx build && fx shell run_test_component fuchsia-pkg://fuchsia.com/inspect_validator_tests#meta/validator_bin_test.cmx && echo Success!
```

To run an integration test to evaluate the Rust Inspect library:
```
--with //src/diagnostics/inspect_validator/lib/rust:tests
fx run-test inspect_validator_test_rust
```
```
fx build && fx shell run fuchsia-pkg://fuchsia.com/inspect_validator_test_rust#meta/validator.cmx && echo Success!
```

To manually run one or more puppets by specifying their URLs (in this case, the Rust puppet):
```
--with //src/diagnostics/inspect_validator
fx build && fx shell run fuchsia-pkg://fuchsia.com/inspect_validator#meta/validator.cmx -- fuchsia-pkg://fuchsia.com/inspect_validator_test_rust#meta/inspect_validator_test_rust.cmx
```

## Source layout

The test entrypoint is located in `src/client.rs`. It connects to and controls
one or more "puppet" programs located under lib/(language) such as
lib/rust/src/main.rs. Since Dart is not currently supported in //src, its
puppet is located at //topaz/public/dart/fuchsia_inspect/test/validator_puppet.



