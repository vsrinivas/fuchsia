This directory contains tests of the fdio library's fdio_spawn functions.

TODO(fxbug.dev/3367): These tests should be colocated with the fdio library and other
fdio tests, but currently cannot be since they require use of component build
rules not available in the //zircon ZN build system. Resolve this once the build
systems are merged.
