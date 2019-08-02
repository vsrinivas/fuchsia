The bootsvc integration tests can currently only be run manually (pending
completion of ZX-4000). To run these tests, follow these steps:

1) Build any product configuration (such as 'bringup' or 'core') or reuse an
existing build directory. Ideally, build for your host architecture so that KVM
can be used when running the tests with QEMU. For example:

```
fx set bringup.x64
fx build
```

2) Boot the bootsvc-integration-tests zbi. For example:

```
$ fx run -z out/default.zircon/bootsvc-integration-tests-x64.zbi -k
```

Note that '-k' (to enable KVM on QEMU) can only be used if the ZBI's
architecture matches your host architecture.

3) All tests should pass and QEMU should exit.  You'll need to verify that
the tests passed by reading the terminal output from `fx run`.
