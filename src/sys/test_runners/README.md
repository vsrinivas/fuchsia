# Test Runners

Reviewed on: 2020-03-11

Test runners help integrate various test frameworks with the [Test Runner
Framework][ftf].

A test runner implements [`fuchsia.component.runner.ComponentRunner`][fidl-component-runner]
to run an underlying test program. It integrates a test framework with
[`fuchsia.test.Suite`][fidl-test-suite].

## Building

```bash
fx set core.x64 --with //src/sys/test_runners
fx build
```

Use the command above to build all test runners available. To build
individual test runners, look for instructions in their respective
subdirectories.

## Running

Look for instructions in test runner's respective subdirectories.

## Testing

Look for instructions in test runner's respective subdirectories.

[ftf]: /docs/concepts/testing/test_runner_framework.md
[fidl-test-suite]: /sdk/fidl/fuchsia.test/suite.fidl
[fidl-component-runner]: /sdk/fidl/fuchsia.component.runner/component_runner.fidl

