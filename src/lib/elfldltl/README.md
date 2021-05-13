# ELF Loading & Dynamic Linking Template Library (`elfldltl`)

This library provides a toolkit of C++ template APIs for implementing ELF
dynamic linking and loading functionality.  It uses C++ namespace `elfldltl`.

The library API supports either 32-bit or 64-bit ELF with either little-endian
or big-endian byte order, via template parameters.  Thus it is suitable for
cross-tools without assumptions about host machine or operating system details,
as well as for ELF target build environments of various sorts.

## Field accessors

ELF format data types are defined using a simple set of generic wrapper
template types for defining fields, defined in
[`<lib/elfldltl/field.h>`](include/lib/elfldltl/field.h).  This API can also be
used directly for other data structures in ELF sections or in memory for which
doing access across bit widths and/or byte orders may be useful.

The wrapper types used for fields have the same exact ABI layout as a specified
underlying integer type.  Their API behavior as C++ objects is roughly like a
normal field of a given integer or `enum` type, in that they are implicitly
convertible to and from that type and can be compared to it for equality.  In
contexts where the C++ type is flexible, these implicit conversions may not be
sufficient and these objects do not fully emulate an integer or `enum` type.
So they also provide direct accessor methods via `field.get()` or `field()`
(i.e. calling the field as a function of no arguments).
