This directory contains tests of the fdio library's fdio_spawn functions.

Other tests of these functions currently live in //zircon/system/utest/spawn.
These tests are separate because they require use of component build rules that
are not currently available in the //zircon ZN build system. In the future,
these two sets of tests can and should be merged, and ideally colocated with the
fdio library.
