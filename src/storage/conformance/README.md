## fuchsia.io Conformance Tests

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
