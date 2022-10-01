# Rust URL parser

This is a C++ library that wraps the Rust `url` crate for parsing URLs. The
API exposed here is the minimum needed for use in fuchsia.git, so if you need
more functionality you may need to expand the coverage before making use of this
library.

New functionality should be covered by both the unit tests and the fuzz test.
