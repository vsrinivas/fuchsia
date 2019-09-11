# The C++ library for loading ICU data

This is the base library for loading ICU data.  Please see the [header file](icu_data.h) for the
API.

The library provides the source file set, which can be used to link directly into a statically
compiled program, and a shared library target, which can be used to link into dynamically linked
programs.

For an example use in the `BUILD.gn` files, see the [build file for the rust
binding](../rust/BUILD.gn).
