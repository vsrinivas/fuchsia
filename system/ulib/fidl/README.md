# The C and C++ fidl library

This library provides the runtime for fidl C bindings. This primarily
means the definitions of the message encoding and decoding
functions. This also includes the definitions of fidl data types such
as vectors and strings.

## Dependencies

This library depends only on the C standard library and the Zircon kernel
public API. In particular, this library does not depend on the C++ standard
library, libfbl.a, or libzx.a.

Some of the object files in this library require an implementation of the
placement new operators. These implementations are typically provided by the
C++ standard library, but they can also be provided by other libraries
(e.g., libzxcpp).

## Idioms

In order to avoid the C++ standard library, this library uses a few unusual
idioms:

### std::move

Rather than using std::move to create an rvalue reference for a type T, this
library uses static_cast<T&&>, which is the language-level construct (rather
than the library-level construct) for creating rvalue references.
