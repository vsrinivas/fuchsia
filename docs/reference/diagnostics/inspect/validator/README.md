# Validator Architecture

Validator applies automated interactive tests to a stateful library such as
Inspect or file systems - an interactive golden file framework.

The Validator architecture includes:

* A set of tests to validate functionality.
* A FIDL protocol to invoke operations to be tested.
* One or more puppet programs which receive FIDL commands and invoke library
calls.
* A reference implementation or simulation of the desired behavior.
* Analysis of puppet results, comparison to local results, and reporting.

## Inspect Validator

The Inspect Validator implementation includes:

* [Core Validator program](/src/diagnostics/inspect_validator/src)
    * [Tests](/src/diagnostics/inspect_validator/src/trials.rs)
    * [FIDL](/src/diagnostics/inspect_validator/fidl/validate.test.fidl)
    * [Reading the puppet's output](/src/diagnostics/inspect_validator/src/data/scanner.rs)
    * [Reference Behavior and comparison](/src/diagnostics/inspect_validator/src/data.rs)
    * [Analysis](/src/diagnostics/inspect_validator/src/runner.rs)
    and [more analysis](/src/diagnostics/inspect_validator/src/metrics.rs)
    * [Reporting](/src/diagnostics/inspect_validator/src/results.rs)
* [Rust Puppet](/src/diagnostics/inspect_validator/lib/rust/src/main.rs).
See also [Inspect Validator Puppet Architecture](puppet.md)
* [Dart Puppet](https://fuchsia.googlesource.com/topaz/+/refs/heads/master/public/dart/fuchsia_inspect/test/validator_puppet/lib/main.dart)
