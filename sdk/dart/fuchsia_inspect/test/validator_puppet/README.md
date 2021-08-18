# Inspect Validator Dart Puppet

Reviewed on: 2019-09-19

Inspect Validator exercises Inspect libraries and evaluates
the resulting VMOs for correctness and (soon) memory efficiency.

To do this, Validator controls "puppet" programs via FIDL, making them
do the actual VMO manipulations using the Inspect library in the target
languages.

This directory contains the Dart Puppet program. BUILD.gn's `:tests` target
invokes the Validator on the Dart Puppet, and is integrated in CQ/CI.

## Building

This project can be added to builds by including
`--with //sdk/dart/fuchsia_inspect/test/validator_puppet:tests`
to the `fx set` invocation.

For example:

```
fx set core.x64 --with //src/diagnostics/validator/inspect:tests --with //sdk/dart/fuchsia_inspect/test/validator_puppet:tests
```

## Running

```
fx build && fx run-test inspect_validator_test_dart
```

## Testing

See Running. Since the Validator puppet integration tests completely
exercise the code in this Puppet, the Puppet does not include unit tests.

## Source layout

lib/main.rs contains the entry point main() which sets up a FIDL service and
dispatches initialization commands and actions to the Dart Inspect library.
