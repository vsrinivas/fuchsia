# gVisor Syscall Tests

## Overview

The gVisor syscall suite contains a large number of tests that validate
POSIX behaviors on loopback sockets. The Netstack team relies heavily
on this suite to prevent regressions and iterate on new functionality.

Test suites are available for three versions of the Netstack:

* [Netstack2](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/connectivity/network/netstack/README.md)
* [Netstack2 with Fast UDP enabled](https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0109_socket_datagram_socket)
* [Netstack3](https://cs.opensource.google/fuchsia/fuchsia/+/main:src/connectivity/network/netstack3/README.md)

## Running the tests

1. `fx set core.qemu-x64 --with //third_party/gvisor_syscall_tests:tests`
1. `fx test netstack{3,2-with-fast-udp,2-with-sync-udp}-syscall-tests`

## Updating the tests

1. Check the commit history in
   `https://github.com/google/gvisor/tree/master/test/syscalls/linux` and insert
   the names of any newly added test sources into `TEST_SRC_FILENAMES.txt`.
1. Run `update.sh` to vendor the latest version of the gVisor test sources
   intree.
1. Add the names of any newly added test sources into the appropriate test
   target in `BUILD.gn`.
1. Build the tests and fix failures:
    *  If a test source includes a new util file under `gvisor/test/util/`, add
       the new filename to `TEST_UTILS_FILENAMES.txt` and rerun `update.sh`.
    *  If a test source or util includes a system header that is not available
       on Fuchsia (e.g. `<linux/capability.h>`), submit a gVisor change that
       adds an appropriate `ifdef` and rerun `update.sh`.
1. Run the tests and fix failures:
    *  Examine the failure and determine whether it is expected.
    *  If the failure *is* expected, add the appropriate expectation (SkipTest,
       ExpectFailure, etc).
    *  If the failure is *not* expected, add the appropriate expectation and
       file a bug.
1. Upload a CL to Gerrit.
