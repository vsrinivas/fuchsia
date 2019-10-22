# Running Fake BTI Tests

1. `fx set workstation.x64 --with-base //garnet/packages/tests:zircon`
2. `fx build`
3. `fx serve`
4. In another terminal: `fx qemu -k -N`
5. At the command prompt: `system/test/sys/fake-bti-test`
