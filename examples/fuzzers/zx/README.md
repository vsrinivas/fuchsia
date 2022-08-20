# Zircon fuzzer

This example shows a trivial fuzzer for Zircon system calls.

## Building

Configure your build to use Kernel Address Sanitizer:

```bash
fx set core.qemu-x64 --with //examples/fuzzers/zx --variant=kasan
fx build
```

## Running

You'll want to open 4 terminals to run this program. In terminal 1, start the
emulator:

```bash
fx qemu -kN
```

In terminal 2, serve package updates:

```bash
fx serve-updates
```

In terminal 3, read the logs:

```bash
fx log
```

In terminal 4, start the component:

```bash
ffx component run fuchsia-pkg://fuchsia.com/example-fuzzers#meta/hello-fuzzy-world.cm --recreate
```

The log should show the component issuing many pointless system calls. The
kernel should safely reject all these calls. If it does not, please [file a
bug](https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Fuzzing+Bug)!
