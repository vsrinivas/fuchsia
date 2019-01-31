# Integral type tests

These tests check that various macros, constants and typedefs in
<stdint.h> and <stddef.h> are correct.

There are two essentially identical source files, one in C and one in
C++. Certain types are built into C++ but not into C, so we need to
test both. Additionally, writing very precise tests comparing values
or types is considerably easier with C++ machinery like decltype and
templated type traits.

Since `wchar_t` in particular is so finicky, it is separately tested
in source files that only include <wchar.h>. In the past there were
bugs where the `wchar_t` header was broken in a way that was masked by
the presence of <stdint.h>, which is very often the case.

This level of detail in tests is valuable in part because compilers
(GCC and Clang) differ in what predefined macros they create. For
instance, sufficiently new GCCs define `__WCHAR_MIN__` as a builtin,
but Clang does not.
