# utest/core

The "core" tests exist for one main purpose:
To test basic functionality when things like devmgr aren't working.

If the kernel is told to run core-tests instead of devmgr, these tests
will run without any userspace device manager, device drivers, io plumbing,
etc.

## Example usage

```
./scripts/run-magenta-x86-64 -c userboot=bin/core-tests
```

## Notes

The tests here are for "core" functionality (message pipes, etc.),
but not all "core" functionality can go here.
For example, you can't start a process in your test because
core tests are for when that functionality isn't working.
Core tests can't use mxio and launchpad uses mxio.

Since these tests can't use mxio core/main.c stubs out the needed
functions from mxio.
