# Integration tests

This directory contains integration tests for `intl_services`.

## Prerequsites

* Make sure you have a device.  Below, the example is given for running the integration test on
  a QEMU emulator.

# Ensure that `fx set-device` is properly set and running.

## IntlServicesTest

An integration test for the interaction between the intl-services.cmx component, the underlying
setui_service.cmx, on top of a fake setup that includes fake stash.  The integration test is a
minimal driver that connects to the write endpoint on `fuchsia.settings.Intl`, and to the read
endpoint of `fuchsia.intl.ProfileProvider`.

The command to compile the test is as follows (assuming `bash` as the driver shell):
```
fx set core.x64 --with=//src/intl/tests && fx build
```

To run the test``:

```
fx run-test intl_services_test
```

