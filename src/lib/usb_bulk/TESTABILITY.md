# Testability

This directory is exempted from automated testing due to hardware dependencies that currently
make automation impractical.

Contents of the `cpp` directory have been forked from Android fastboot with only trivial
modifications. They are not directly tested in their current form, but tests should be added
to cover any substantive changes that are made in the future.

The `rust` library implements a Rust wrapper of the C++ library. The tests in `lib.rs` are
designed to be run manually using a specific hardware configuration, as documented inline.
