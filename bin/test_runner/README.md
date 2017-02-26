`test_runner` is a TCP daemon that accepts connections, reads test commands and
executes them. It is meant to provide a way to run tests from the host and get
back results.

On fuchsia, each test is run in a new application environment, which exposes a
service that the tests can use to report completion (particularly useful for
integration tests).

INSTRUCTIONS
============

Prerequisite:
- An instance of magenta running (on qemu or real device), configured with networking. For
  example, see [networking configuration
  doc](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only).
- A build configuration that runs `test_runner` at startup. For example,
  `./package/gn/gen.py -m boot_test_modular`.

*Testing on a qemu instance:*
Run a test using `//apps/modular/src/test_runner/run_test`. E.g:

```sh
$ $FUCHSIA_DIR/apps/modular/src/test_runner/run_test "bootstrap device_runner --user_shell=dummy_user_shell"
```

This will return when it has completed (either by succeeding or crashing). You
can watch the qemu console to see any console output written by test. In case of
a crash, this console output will be dumped by `run_test`.

You can also run a series of tests by supplying a JSON file describing the tests to run:
```sh
$ $FUCHSIA_DIR/apps/modular/src/test_runner/run_test --test_file=$FUCHSIA_DIR/apps/modular/tests/modular_tests.json
```

*Testing on a real device:*
You must specify the IP address of the real device, and pass it in using
`run_test`'s `--server` flag.

Troubleshooting:
- If `run_test` is having trouble finding your qemu instance, your instance may
  be assigned a different IP than the default that `run_test` assumes. If
  incorrect, you can pass the assigned IPv4 address by looking at the device log
  of the qemu. The line to look for:
``` [00006.470] 01216.01434> ip4_addr: 192.168.3.53 netmask: 255.255.255.0 gw: 192.168.3.1 ```
  Pass the correct `ipv4_addr` to the `run_test` tool:
``` $FUCHSIA_DIR/apps/modular/src/test_runner/run_test --server 192.168.3.53:8342 ... ```
- The `FUCHSIA_DIR` env variable is set, for example from sourcing `//scripts/env.sh`.

TEST CONFIG DESCRIPTION
=======================

This describes the config format for the test file supplied to the |run_test|
script for running multiple tests.

By example:

```
{
    "tests": [
        {
          "name": "dummy_user_shell",
          "exec": "bootstrap device_runner --user_shell=dummy_user_shell"
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
