# utest/core

The "core" tests exist for one main purpose:
To test basic functionality when things like devmgr aren't working.

There are two different ways in which core tests are built and run:
unified and standalone.

## Unified mode

In this mode, all core tests are built into a single executable that
is directly invoked by the kernel. That is, the kernel is told to run
core-tests instead of devmgr.  In this mode the tests will run without
any userspace device manager, device drivers, io plumbing, etc.

### Example usage

```
fx set core.x64   # any of {bringup,core}.{x64,arm64} are fine too.
fx build
fx core-tests [--gtest_filter=FILTER] [--gtest_repeat=REPEAT]
```

The helper fx command runs QEMU (or FEMU) providing the
specially-built core-tests.zbi as a `-z` argument.

Only a subset of tests can be specified by `gtest_filter`, e.g.
`--gtest_filter="FutexTest.*"`

Tests can be rerun with `gtest_repeat`, e.g.
`--gtest_repeat=100` to run all tests 100 times or
`--gtest_repeat=-1` to run all tests until a test fails.

## Standalone mode

In this mode, each test is built into its own binary which can be run
"manually" from a serial shell or via runtests.  Not all core tests
can operate in standalone mode.  For example, some tests require the
root resource which is only available to tests running in unified
mode.  See `unified_only` in BUILD.gn for a list of such tests.

### Example usage

```
fx set bringup.x64 --with-base //bundles/bringup:tests
fx build
fx qemu
```

Then at the shell prompt,

```
/boot/test/core-futex --gtest_filter='FutexTest.Wakeup' --gtest_repeat=10
```

or

```
runtests /boot/test/core-*
```

## Notes

The tests here are for "core" functionality (channels, etc.), but
not all "core" functionality can go here.  For example, you can't
start a process in your test with launchpad because core tests are for
when that functionality isn't working.  Core tests can't use fdio and
launchpad uses fdio.

Since these tests can't use fdio core/libc-and-io-stubs.c stubs out the needed
functions from fdio.
