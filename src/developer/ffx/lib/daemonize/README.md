# daemonize

This is a small wrapper around daemon(3).

## Safety

The implementation does not violate any of the constraints documented on
`Command::pre_exec`, and this code is expected to be safe.

This code may however cause a process hang if not used appropriately. Reading on
the subtleties of CLOEXEC, CLOFORK and forking multi-threaded programs will
provide ample background reading. For the sake of safe use, callers should work
to ensure that uses of `daemonize` occur early in the program lifecycle, before
many threads have been spawned, libraries have been used or files have been
opened that may introduce CLOEXEC behaviors that could cause EXTBUSY outcomes in
a Linux environment.
