# test_list_tool: the test-list generator

test_list_tool is a host tool that takes tests.json as input and generates a
new file called test-list.json. This file contains metadata about the tests,
and can include data scraped from generated component manifest files.

## Schema

See the [test-list crate](/src/lib/testing/test_list/README.md) for the latest test-list.json schema.

### Tags

This tool extends the test-list.json schema with a specific set of tags that will be present for all tests in the list. The specific tags are as follows:

| Key | Description | Source |
|---|---|---|
| os | The operating system the test is intended to run on. For example: "linux" or "fuchsia" | Copied from tests.json, which is output by the build. |
| cpu | The CPU this test is intended to run on. For example: "x64" or "arm64".  | Copied from tests.json, which is output by the build. |
| scope | The scope of the test. For example: "unit" or "integration". | See description below this table. |
| realm | The test realm that the on-device Test Manager will run this test in. For example: "hermetic" or "system". | Copied from the "fuchsia.test" facet of the test's component manifest file. |
| hermetic | "true" if this test is hermetic, "false" if not hermetic, empty if not applicable. | A test is hermetic if its realm is "hermetic". |
| legacy_test | "true" if this test is a legacy test, empty otherwise. | A test is a legacy test if its component URL ends in ".cmx" |

#### Test Scopes

This section describes how we categorize a [Test's
Scope](https://fuchsia.dev/fuchsia-src/contribute/testing/scope?hl=en),
which tells us if a test is treated as a "unit test," "integration
test," "system test," etc.

The valid scopes supported by this tool are:

| Scope | Heuristic |
|---|---|
| host | The test points to a host binary, not a Fuchsia package. |
| host_shell | The test points to a host shell binary, not a Fuchsia package. |
| unit | The test runs in a hermetic realm and uses a generated default manifest. |
| integration | The test runs in a hermetic realm and uses a non-generated custom manifest. |
| system | The test runs in a non-hermetic realm. |
| wrapped_legacy | The test uses the legacy_test_runner and wraps a v1 test. These tests are not hermetic. |
| fuzzer | The test uses the "fuzzer_package" build rule, and it is hermetic. |
| prebuilt | The test uses the "prebuilt_test_package" build rule and does not run in a non-hermetic realm. |
| bootfs | The test is an ELF binary executed out of /boot on the device. |
| unknown | The test uses an unknown build rule (see algorithm below). |
| uncategorized | The test does not fall into any of the above categories. |

We apply these heuristics as follows:

1. Check if the test's name begins with `host_` or `linux_`. These
are **host** tests.
   - Justification: Host test names in fuchsia.git always begin
   with `host_` and no other tests do. There are some tests for
   Linux that start with `linux_`, and those tests also are scoped
   to the host.
1. Check if the test's name ends with `_host_test.sh`. These are **host_shell** tests.
   - Justification: Some host test names end with this suffix, and
   they are executed as shell scripts on the host.
1. If the build rule that generated the test is not listed in the
following section , the test type is **unknown**.
   - Justification: We do not want to categorize tests using other
   rules just yet. This leaves us an opportunity to separately
   handle other types of tests without miscategorizing them up
   front.
1. If the test does not run in the `hermetic` realm it is a **system**
test.
   - Justification: This realm enforces hermetic execution of
   tests, and all other tests can change and depend on global system
   state.
1. If the test used the `fuzzer_package` build rule, it is a **fuzzer** test.
   - Justification: Fuzzers typically have a mode of execution where
   they complete a single iteration of fuzzing with fixed input to
   ensure the fuzzer setup itself is bug free. These types of tests
   are treated separately from general unit and integration tests
   to track the impact they have on our build.
1. If the test used the `prebuilt_test_package` build rule, it is a **prebuilt** test.
   - Justification: Prebuilt tests are rolled into this repository
   from an external source and run on the Fuchsia platform defined
   by this repository. We want to track how many tests are doing
   this, and their overall impact on test health.
1. If the test used the `bootfs_test` build rule, it is a **bootfs** test.
   - Justification: Bootfs tests are simply ELF binaries run as
   processes directly out of `/boot` on the device. We want to track
   how many tests do this.
1. If the test uses a generated manifest, it is a **unit** test.
   - Justification: The generated manifest does not provide the
   ability to start additional components or processes, so the
   entire test is contained in a single process.
1. If the test uses an explicit manifest, it is an **integration** test.
   - Justification: The primary reason to use a non-generated
   manifest is to start other components as in the scope of a test.
   Tests that start and communicate between multiple components are
   integration tests by definition.

The supported build rules are:

* `fuchsia_unittest_package` - Convenience rule for wrapping a single test in a package with a generated manifest.
* `fuchsia_test_package` - Rule for including one or more test components in a package.
* `fuchsia_test` - Rule for wrapping a single test component in a package.
* `fuzzer_package` - Rule for defining a fuzzer and associated test.
* `prebuilt_test_package` - Rule for wrapping a prebuilt test binary from another repository as a test package.