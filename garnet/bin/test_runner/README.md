# Test Runner for Integration Tests
`run_integration_tests` is a binary that can execute a command line in an
environment that provides services that allow multiple test components to
orchestrate joint asynchronous distributed flows of control. It is used for
integration tests of the modular framework, for example.

Multiple such command lines can be configured in a single configuration file.

## Test Config Description

The JSON file specified by `--test_file` parameter looks similar to this:

```
{
  "tests":[
    {
      "name":"dummy_session_shell",
      "exec":[
        "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx ",
        "--ledger_repository_for_testing ",
        "--base_shell=fuchsia-pkg://fuchsia.com/dummy_base_shell#meta/dummy_base_shell.cmx ",
        "--session_shell=fuchsia-pkg://fuchsia.com/dev_session_shell#meta/dev_session_shell.cmx"
      ]
    },
    {
      "name":"parent_child",
      "exec":[
        "fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx ",
        "--ledger_repository_for_testing ",
        "--base_shell=fuchsia-pkg://fuchsia.com/dummy_base_shell#meta/dummy_base_shell.cmx ",
        "--session_shell=fuchsia-pkg://fuchsia.com/dev_session_shell#meta/dev_session_shell.cmx ",
        "--session_shell_args=--root_module=fuchsia-pkg://fuchsia.com/parent_module#meta/parent_module.cmx"
      ]
    }
}
```

The top-level `tests` field is a list of tests to run, sequentially.
Each test is an object with the following fields:

- `name`
  - Required.
  - A string field identifying the test.
- `exec`
  - Required. Must not be empty.
  - A string with the command representing the test to run. This will be run in
    a new application environment with a TestRunner service, which some part of
    the test is expected to use to report completion. Must not be the empty string.
  - Alternativey, an array of strings. The first element is the component to run,
    all following elements are the args given to the component. Must not be the
    empty array. The first element must not be the empty string.
- `disabled`
  - Optional.
  - Must be a boolean.
  - If the boolean is true, the test is not executed.
