## fuchsia.io Conformance Tests

The fuchsia.io conformance tests are designed to exercise the fuchsia.io interface.
There is a common suite of conformance tests, written in Rust (in `tests/`),
which are run against the various conformance test harnesses.  These harnesses
(in the `conformance_harness` directory) indicate which functionality should be
tested, and proxy requests from the test driver to the underlying client library.

This contrasts with the tests in `fs_test`, which are designed to exercise the
FDIO/POSIX interface (and typically assume the underlying filesystem is mutable).

These tests ensure that fuchsia.io servers behave as expected under various conditions. It does
this by setting up specific node and directory layouts using fuchsia.io server libraries. Then
server handling of various protocol invariants are validated using FIDL.

In order to set up servers in different languages, we create a test driver for
each filesystem server. A test driver will be a component that could be
launched by the conformance test suite on demand, and serve a number of
directories via the `fuchsia.io.test` FIDL protocol, using a specific filesystem
library.

## Test source layout

The conformance tests exist in `tests/`, with `tests/tests.rs` being the root. Each file exercises
a targeted subset of FIDL methods on each of the Node, File, and Directory protocols. Files in
`tests/` test general Node methods, files in `tests/directory/` test methods specific to Directory,
and files in `tests/file/` test methods specific to File.

Common utilities are provided by the library in `src/`, which is compiled as the
`io_conformance_util` crate. This includes utilities to create the structures the harnesses will
use as direction to create directory trees for testing, functions to open concrete types, and some
common asserts.
