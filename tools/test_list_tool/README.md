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
| unit | The test runs in a hermetic realm and uses a generated default manifest. |
| integration | The test runs in a hermetic realm and uses a non-generated custom manifest. |
| system | The test runs in a non-hermetic realm. |
| wrapped_legacy | The test uses the legacy_test_runner and wraps a v1 test. These tests are not hermetic. |
| unknown | The test uses an unknown build rule (see algorithm below). |
| uncategorized | The test does not fall into any of the above categories. |

We apply these heuristics as follows:

1. Check if the test's name begins with `host_`. These are **host** tests.
   - Justification: Host test names in fuchsia.git always begin with `host_` and no other tests do.
2. If the build rule that generated the test is not `fuchsia_unittest_package` or `fuchsia_test_package`, the test type is **unknown**.
   - Justification: We do not want to categorize tests using other rules just yet. This leaves us an opportunity to separately handle other types of tests without miscategorizing them up front.
3. If the test does not run in `hermetic` or `hermetic-tier-2` realms it is a **system** test.
   - Justification: These realms enforce hermetic execution of tests, and all other tests can change and depend on global system state.
4. If the test uses a generated manifest, it is a **unit** test.
   - Justification: The generated manifest does not provide the ability to start additional components or processes, so the entire test is contained in a single process.
5. If the test uses an explicit manifest, it is an **integration** test.
   - Justification: The primary reason to use a non-generated manifest is to start other components as in the scope of a test. Tests that start and communicate between multiple components are integration tests by definition.
