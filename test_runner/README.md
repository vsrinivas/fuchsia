
test_runner is a TCP daemon that accepts connections, reads test commands and
executes them. It is meant to provide a way to run tests from the host and get
back results.

On fuchsia, each test is run in a new application environment, which exposes a
service that the tests can use to report completion (particularly useful for
integration tests).

INSTRUCTIONS
============

Prerequisite:
- An instance of magenta running (qemu or otherwise), configured with network
  that host (Linux or Mac) can interface over. For example, see
  [networking configuration doc](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/qemu.md#Enabling-Networking-under-QEMU-x86_64-only).

Run a test using //apps/modular/test_runner/tools/run_test. E.g:
```
$ $FUCHSIA_DIR/apps/modular/test_runner/tools/run_test "/system/apps/bootstrap /system/apps/device_runner --user-shell=/system/apps/dummy_user_shell"
```

This will return when it has completed (either by succeeding or crashing). You
can watch the qemu console to see any console output written by test. In case of
a crash, this console output will be dumped by `run_test`.

You can also run a series of tests by supplying a JSON file describing the tests
to run:
$ $FUCHSIA_DIR/apps/modular/test_runner/tools/run_test --test_file=$FUCHSIA_DIR/apps/modular/test_runner/tools/modular_tests.json

Troubleshooting:
- If `run_test` is having trouble finding your qemu instance, your instance may
  be assigned a different IP than the default that `run_test` assumes. If
  incorrect, you can pass the assigned IPv4 address by looking at the device log
  of the qemu. The line to look for:
``` [00006.470] 01216.01434> ip4_addr: 192.168.3.53 netmask: 255.255.255.0 gw: 192.168.3.1 ```
  Pass the correct `ipv4_addr` to the `run_test` tool:
``` $FUCHSIA_DIR/apps/modular/test_runner/tools/run_test --server 192.168.3.53:8342 ... ```
- The `FUCHSIA_DIR` env variable is set, for example from sourcing `//scripts/env.sh`.
