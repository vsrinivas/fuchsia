# Running Fake BTI Tests

1. `fx set core.x64 --with //garnet/packages/tests:zircon`
2. `fx build`
3. `fx serve`
4. In another terminal: `fx qemu -k -N`
4. In another terminal: `fx test -o src/devices/testing/fake-bti`
