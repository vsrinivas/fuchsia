# Expectation-based testing

A system enabling a Fuchsia test package to encode pass/fail expectations and
skipped cases for a test package. This is particularly helpful for
conformance-style test suites where the test suite itself is cleanly separable
from the system under test.

With this system enabled, expected-to-fail tests that unexpectedly pass will
register as test failures, and vice versa, which is helpful to prompt changes to
test expectations to prevent backsliding.

## Usage

To use test expectations, `fuchsia_test_with_expectations_package` must be used
instead of `fuchsia_test_package`:

```gn
import(
    "//src/lib/testing/expectation/fuchsia_test_with_expectations_package.gni")

fuchsia_test_with_expectations_package("expectation-example-package") {
  test_components = [ ":some-integration-test" ]
  expectations = "expectations_file.json5"
}
```

Each component specified in `test_components` must have its manifest `include`
the expectation client shard (./meta/client.shard.cml). This is enforced by the
`fuchsia_test_with_expectations_package` GN template.

See the doc comments in ./fuchsia_test_with_expectations_package.gni for more
information.

Pass/fail/skip expectations are specified via a JSON5 file.
See `./example_expectations.json5` for a simple example.
