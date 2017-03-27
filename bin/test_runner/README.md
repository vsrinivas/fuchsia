# Test Runner
`test_runner` is a TCP daemon that runs on Fuchsia to accept connections, read
test commands and execute them. It provides a way to run tests from the host
and get back results.

On Fuchsia, each test is run in a new application environment, which exposes a
service that the tests can use to report completion (particularly useful for
integration tests).

## Instructions

Prerequisite:
- An instance of magenta running (on qemu or real device), configured with networking. For
  example, see [networking configuration
  doc](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Enabling-Network).
- A build configuration that runs `test_runner` at startup. For example,
  `./package/gn/gen.py -m boot_test_modular`.

#### Testing on a qemu instance

Run a test using `//apps/test_runner/src/run_test`. E.g:

```sh
$ $FUCHSIA_DIR/apps/test_runner/src/run_test "device_runner --user_shell=dummy_user_shell"
```

This will return when it has completed (either by succeeding or crashing). You
can watch the qemu console to see any console output written by test. In case of
a crash, this console output will be dumped by `run_test`.

You can also run a series of tests by supplying a JSON file describing the tests to run:
```sh
$ $FUCHSIA_DIR/apps/test_runner/src/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json
```

#### Testing on a real device
You must specify the IP address of the real device by using
`run_test`'s `--server` flag.

Troubleshooting:
- If `run_test` is having trouble finding your qemu instance, your instance may
  be assigned a different IP than the default that `run_test` assumes. If
  incorrect, you can pass the assigned IPv4 address by looking at the device log
  of qemu. Look for this line:

  ``` [00006.470] 01216.01434> ip4_addr: 192.168.3.53 netmask: 255.255.255.0 gw: 192.168.3.1 ```

  Pass the correct `ipv4_addr` to the `run_test` tool:

  ``` $FUCHSIA_DIR/apps/test_runner/src/run_test --server 192.168.3.53:8342 ... ```

- The `FUCHSIA_DIR` env variable is set, for example from sourcing `//scripts/env.sh`.

## Test Config Description

You can control the testing configuration by passing `run_test` the name of a
JSON file with the `--test_file` parameter. For example:

```
${FUCHSIA_DIR}/apps/test_runner/src/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json
```

The JSON file looks similar to this:

```
{
    "tests": [
    {
      "name":"dummy_user_shell",
      "exec":"device_runner --ledger_repository_for_testing --user_shell=dummy_user_shell"
    },
    {
      "name":"parent_child",
      "exec":"device_runner --ledger_repository_for_testing --user_shell=dev_user_shell --user_shell_args=--root_module=/system/apps/modular_tests/parent_module",
      "copy":{
        "/system/apps/modular_tests":[
          "parent_module",
          "child_module"
        ]
      }
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
- `copy`
  - A map field where each key is the name of a directory on the Fuchsia
  system and each name value of the array is the name of a file on the host
  to be copied to that directory. The `copy` field is optional. If absent,
  the files currently on the device will be used.
