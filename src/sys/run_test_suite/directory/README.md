# Fuchsia Test Output Directory Schema

Reviewed on: 2021-07-21

When tests are executed with `ffx test`, the results and any artifacts are
stored in a structured format in a directory on the host. This structured
format is intended for consumption by tools to display or analyze test results,
and to convert to other formats.

Note: As `ffx test` supports only v2 components, this format does not apply to
tests running as v1 components.

## Background

A single execution of `ffx test` produces a *test run*. A *test run* is
comprised of *suites*, and each suite is comprised of *test cases*.

A *suite* is an execution of a test component. The test component is most
commonly identified by its URL, for example,
`fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm`.

A *test case* is an execution of a single test case contained in a suite.

In addition, test runs, suites, or test cases may produce artifacts. These
include outputs from the test such as stdout, stderr, and syslog.

## Directory structure
The root of an output directory contains one or more JSON files that describe
the test result, and subdirectories for any artifacts.

### JSON files

The entry point is `run_summary.json`. This file contains the overall result of
the test run, a description of the subdirectory containing any artifacts scoped
to the test run, and a list of the JSON files containing results for each
suite. JSON files for suite results contain the overall results for the suite
and for any test cases it contains, as well as the names of subdirectories for
any artifacts scoped to each test case or suite. Note that the names of the
suite summary files and artifact subdirectories is not stable. Tools should
parse the JSON files to find the correct names.

See the [schemas](./schema) for the exact schema of the JSON files.

### Artifact subdirectories

Each test run, suite, and case may have an artifact subdirectory. These
subdirectories are populated with artifacts produced by running tests, such as
stdout or syslog. The JSON files describe the contents of the artifact
subdirectories.

## Future work

1. Publish schemas in the SDK (fxbug.dev/81195)
1. Support arbitrary artifacts (fxbug.dev/75690)
