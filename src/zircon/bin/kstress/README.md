# kstress

The `kstress` command runs VM stress tests. Some VM stress tests use syscalls from the `next` vDSO,
and so need to be run as a component with the `use_next_vdso` flag. Therefore the VM stress tests
should be run as the test component `vm-stress-test.cm` instead of using the `kstress` command
directly at the shell prompt.

Build and run the test component like so:

```
fx set core.x64 --with src/zircon/tests/stress-tests:tests
fx build
fx qemu
fx test vm-stress-test
```
