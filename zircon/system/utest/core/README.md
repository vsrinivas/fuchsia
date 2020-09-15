# utest/core

The "core" tests exist for one main purpose:
To test basic functionality when things like devmgr aren't working.

If the kernel is told to run core-tests instead of devmgr, these tests
will run without any userspace device manager, device drivers, io plumbing,
etc.

## Example usage

```
fx set core.x64   # any of {bringup,core}.{x64,arm64} are fine too.
fx build
fx core-tests [--gtest_filter=FILTER] [--gtest_repeat=REPEAT]
```

The helper fx command runs AEMU providing the specially-built core-tests.zbi as
a `-z` argument.

Only a subset of tests can be specified by `gtest_filter`, e.g.
`--gtest_filter="FutexTest.*"`

Tests can be rerun with `gtest_repeat`, e.g.
`--gtest_repeat=100` to run all tests 100 times or
`--gtest_repeat=-1` to run all tests until a test fails.

## Notes

The tests here are for "core" functionality (channels, etc.), but
not all "core" functionality can go here.  For example, you can't
start a process in your test with launchpad because core tests are for
when that functionality isn't working.  Core tests can't use fdio and
launchpad uses fdio.

Since these tests can't use fdio core/libc-and-io-stubs.c stubs out the needed
functions from fdio.
