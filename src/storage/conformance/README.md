## fuchsia.io Conformance Tests

The fuchsia.io conformance tests are designed to exercise the fuchsia.io interface.
There is a common suite of conformance tests, written in Rust (`io1_tests.rs`),
which are run against the various conformance test harnesses.  These harnesses
(in the `conformance_harness` directory) indicate which functionality should be
tested, and proxy requests from the test driver to the underlying client library.

This contrasts with the tests in `fs_test`, which are designed to exercise the
FDIO/POSIX interface (and typically assume the underlying filesystem is mutable).
There are a small amount of tests written in C++ which exercise the FDIO interface
(see `fdio.cc`).

These tests ensure that the different fuchsia.io clients and servers can
interop successfully. It does this by setting up specific node and directory
layouts using fuchsia.io server libraries. Thereafter:

- The server handling of various protocol invariants are validated using FIDL.

- The correct operation of client libraries are tested against these
  specific layouts.

In order to set up servers in different languages, we create a test driver for
each filesystem server. A test driver will be a component that could be
launched by the conformance test suite on demand, and serve a number of
directories via the `fuchsia.io.test` FIDL protocol, using a specific filesystem
library.
