# Fuchsia Test Output Directory Schema

Reviewed on: 2022-01-05

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
suite summary files, artifact subdirectories, and artifact file names are not
stable. Tools should parse the JSON files to find the correct names.

See the [schemas][schemas] for the exact schema of the JSON files.

### Artifact subdirectories

Each test run, suite, and case may have an artifact subdirectory. These
subdirectories are populated with artifacts produced by running tests, such as
stdout or syslog. An artifact may be either a file or a directory of files.

The JSON files describe the location of each subdirectory in an `artifact_dir`
field, and the contents of the subdirectory in an `artifacts` field. The
`artifacts` field contains a mapping from the location of the artifact to
metadata. The metadata describes information including the type of artifact,
and in some cases, the moniker of the component that produced the artifact.
Note that in the case of a directory based artifact, only the location of the
root directory of the artifact is listed.

### Artifact types

This may not be exhaustive. See the [schemas][schemas] for a full list of
supported artifacts.

| Type           | Format    | Description                                                   |
| -------------- | --------- | ------------------------------------------------------------- |
| SYSLOG         | File      | Syslog collected from all components running in a test suite. |
| RESTRICTED_LOG | File      | Any high severity logs that caused a test to be failed.       |
| STDOUT         | File      | stdout collected from a single component in a test.           |
| STDERR         | File      | stderr collected from a single component in a test.           |
| REPORT         | File      | A copy of the stdout output produced by ffx test.             |
| CUSTOM         | Directory | A set of arbitrary files produced by a component in a test.   |

## Future work

1. Publish schemas in the SDK (fxbug.dev/81195)

[schemas][./schemas]
