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
      "name":"dummy_user_shell",
      "exec":"basemgr --ledger_repository_for_testing --device_shell=dummy_device_shell --user_shell=dummy_user_shell"
    },
    {
      "name":"parent_child",
      "exec":"basemgr --ledger_repository_for_testing --device_shell=dummy_device_shell --user_shell=dev_user_shell --user_shell_args=--root_module=/system/test/modular_tests/parent_module"
    }
}
```

The top-level `tests` field is a list of tests to run, sequentially.
Each test is an object with the following fields:

- `name`
  - A string field identifying the test. Required.
- `exec`
  - A string with the command representing the test to run. This will be run in
    a new application environment with a TestRunner service, which some part of
    the test is expected to use to report completion.
- `disabled`
  - If this field is present, the test is not executed.
