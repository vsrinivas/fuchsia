# Test Runner
`test_runner` is a TCP daemon that runs on Fuchsia to accept connections, read
test commands and execute them. It provides a way to run tests from the host
and get back results.

On Fuchsia, each test is run in a new application environment, which exposes a
service that the tests can use to report completion (particularly useful for
integration tests).

## Instructions

#### Prerequisites
- An instance of zircon running (on QEMU or real device), configured with
  networking. For example, see [networking configuration
  doc](https://fuchsia.googlesource.com/docs/+/master/getting_started.md#Enabling-Network).
  For QEMU, see also the warning below.

- A build configuration that runs `test_runner` at startup. For example,
  `./package/gn/gen.py -m boot_test_modular`. Alternatively, you can run
  `test_runner` from the shell.

- The `FUCHSIA_` environment variables used below are set by sourcing
  `//scripts/env.sh`.

*** note
**QEMU**: `test_runner` doesn't currently work with the default networking
setup (TO-292). The workaround is to pass the following flags to QEMU: `-device
e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp::8342-:8342` and then run
`run_test --server 127.0.0.1 <other arguments>`.
***

#### Running the tests

The script will automatically search for a Zircon device on the local
subnet and use it. This discovery is performed using ipv6. This process
works for both QEMU and for real hardware, but not for the
[Fuchsia test infrastructure](#Fuchsia-test-infrastructure).

Run a test using `//apps/test_runner/src/run_test`. For example:

```sh
$ $FUCHSIA_DIR/apps/test_runner/src/run_test "device_runner --ledger_repository_for_testing --device_shell=dummy_device_shell --user_shell=dev_user_shell --user_shell_args=--root_module=/system/apps/modular_tests/agent_trigger_test"
```

This will return when it has completed (either by succeeding or crashing). You
can watch the QEMU console to see any console output written by test. In case of
a crash, this console output will also be dumped by `run_test`.

You can also run a series of tests by supplying a JSON file describing the
tests to run:

```sh
$ $FUCHSIA_DIR/apps/test_runner/src/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json
```

#### Selecting between multiple devices

If you have more than one device, you can select a particular device with
`run_test`'s `--server` flag. The device name is displayed on the boot screen,
or you can view all connected devices with the netls command, like this:

```sh
$FUCHSIA_OUT_DIR/build-zircon/tools/netls
```

Pass the device name to the `run_test` tool:

```sh
$FUCHSIA_DIR/apps/test_runner/src/run_test --server rain-detour-glaze-donut ...
```

#### Fuchsia test infrastructure

For the Fuchsia automated test infrastructure, QEMU is configured to
transparently route 127.0.0.1:8342 to a known IP address. ipv6 is not
supported. `run_test` supports this environment by allowing you to specify an
ipv4 address for the `--server` parameter. However, there are caveats:

  - `loglistener` does not work because it requires ipv6. Therefore, no
    logging data will be captured if a test has errors.
  - The `--sync` parameter is not supported.


## Test Config Description

You can control the testing configuration by passing `run_test` the name of a
JSON file with the `--test_file` parameter. For example:

```
$FUCHSIA_DIR/apps/test_runner/src/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json
```

You can select a particular test to run by passing `run_test` the --test_name
parameter in addition to the name of a JSON file with the `--test_file`
parameter. For example:

```
$FUCHSIA_DIR/apps/test_runner/src/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json --test_name=parent_child
```

The JSON file looks similar to this:

```
{
  "tests":[
    {
      "name":"dummy_user_shell",
      "exec":"device_runner --ledger_repository_for_testing --device_shell=dummy_device_shell --user_shell=dummy_user_shell"
    },
    {
      "name":"parent_child",
      "exec":"device_runner --ledger_repository_for_testing --device_shell=dummy_device_shell --user_shell=dev_user_shell --user_shell_args=--root_module=/system/apps/modular_tests/parent_module",
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
  the files currently on the device will be used. You must pass the --sync
  flag to `run_test` to cause the files to be copied. This does not work
  with QEMU devices accessed with ipv4.
