# FIDL Cross-Petal Change Tests

These are tests to validate source compatibility through various changes to FIDL
libraries. They do not test binary compatibility.

## Libraries
Under `fidl/` there are three libraries before, during and after reflecting the
state of a FIDL library before a change has begun, during the transition and
after the transition is complete.

## C++
The C++ tests consist of four executables that are built but not run since
compile-time source compatibility is what is being validated.

 - `cpp_before` compiles `cpp/before.cc` against the _before_ library
 - `cpp_during_1` compiles `cpp/before.cc` against the _during_ library
 - `cpp_during_2` compiles `cpp/after.cc` against the _during_ library
 - `cpp_after` compiles `cpp/after.cc` against the _after_ library
