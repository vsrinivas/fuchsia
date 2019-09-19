# Session Manager Usage Guide

## To launch session_manager

```
fx set core.x64 --with //src/session:session_framework
fx shell run fuchsia-pkg://fuchsia.com/component_manager_sfw#meta/component_manager_sfw.cmx fuchsia-pkg://fuchsia.com/session_manager#meta/session_manager.cm
```

## To run the tests

Run `fx shell ifconfig` to find the IP address of the device that will run the test.

Use the following command to launch the set of Session Framework tests defined in `test_plan.json`.
Tests listed in `test_plan.json` can be v1 or v2 components. Every test included in this list needs
to expose `/svc/fuchsia.test.Suite`.

This command starts the test_runner which uses component_manager to launch each test as a component.
Note that these tests are not run in CQ.

```
FUCHSIA_IPV4_ADDR=<IP OF DEVICE> FUCHSIA_SSH_KEY=.ssh/pkey ./out/default/host_x64/dart-tools/test_runner -p src/session/session_manager_integration_tests/test_plan.json
```

## Debug tips

If the test hangs after these lines:
```
creating sl4f driver
starting sl4f driver
```

you may need to reconfigure your network setup.