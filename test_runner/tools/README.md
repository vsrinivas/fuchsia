Test config description
=======================

This describes the config format for the test file supplied to the |run_test|
script for running multiple tests.

By example:

```
{
    "tests": [
        {
          "name": "dummy_user_shell",
          "exec": "/system/apps/bootstrap /system/apps/device_runner --user_shell=file:///system/apps/dummy_user_shell"
        },
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
