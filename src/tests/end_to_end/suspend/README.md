# Suspend

The `suspend_test` verifies that the device enters the S3 suspend to RAM power
state, and attempts to resume when the power button is pressed. At this point
the device is not expected to resume successfully.

This test is only enabled on Atlas devices since the device-specific EC serial
line is used to verify that the device is in the correct power state, and to
send the resume signal.

To build the test:

```
% fx set ... --with //src/tests/end_to_end/suspend:test --args 'enable_suspend=true'
% fx build
```

Since the `enable_suspend` flag is set to false by default, disabling suspend
on the device, it is necessary to push this build to the device before running
the test.

```
% fx ota
```

To run the test:

```
% $(fx get-build-dir)/host_x64/e2e_suspend_test --ssh-private-key ~/.ssh/fuchsia_ed25519
```
