# The C and C++ fidl library

This library provides the runtime for fidl C bindings. This primarily
means the definitions of the message encoding and decoding
functions. This also includes the definitions of fidl data types such
as vectors and strings.

## Dependencies

This library depends only on the C standard library, the Zircon kernel
public API, and some header-only parts of the C++ standard library.
In particular, this library does not depend on libfbl.a or libzx.a.
It also does not link against the C++ standard library.

Some of the object files in this library require an implementation of the
placement new operators from the standard library's <new> header file.
These are usually implemented as inlines in the header.
