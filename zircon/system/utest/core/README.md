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
fx core-tests [gtest-filter]
```

The helper fx command runs AEMU providing the specially-built core-tests.zbi as
a `-z` argument.

`gtest-filter` can be optionally specified to run only the given gtest suite,
e.g. `fx core-tests 'FutexTest.*'`.

## Notes

The tests here are for "core" functionality (channels, etc.), but
not all "core" functionality can go here.  For example, you can't
start a process in your test with launchpad because core tests are for
when that functionality isn't working.  Core tests can't use fdio and
launchpad uses fdio.

Since these tests can't use fdio core/libc-and-io-stubs.c stubs out the needed
functions from fdio.
