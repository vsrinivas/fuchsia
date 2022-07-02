# maybe-standalone-test library

This is a companion library to the adjacent [standalone-test] library.
Individual test modules can use this library instead of that one so that they
can be built into either of two kinds of tests:

 * A standalone test executable launched directly by userboot.

 * Other test executables that run on full-fledged Fuchsia systems.

Test code uses this library to be flexible and adapt at runtime to either
situation so the individual test modules don't have to be recompiled separately
for the two cases.  Each `maybe_standalone::Get*` API corresponds to a similar
API in `standalone::Get*` from the companion [standalone-test] library but it
can return a null result where the `standalone::Get*` function always returns a
valid result (or panics).  Flexible test code skips tests or otherwise changes
behavior that it can't do outside the standalone test context.

A standalone test executable explicitly depends on [standalone-test] (and
minimizes its other dependencies, such as avoiding [fdio]).  This causes any
modules (e.g. `source_set()` targets) in its `deps` that themselves use
`maybe_standalone` APIs to get valid results from those calls.  Such modules
should take care to keep their own dependencies minimal as well.

Flexible test modules use this `maybe-standalone` library but _do not_ refer to
the [standalone-test] library directly.  When built into a different test
executable that doesn't use [standalone-test], their `maybe_standalone` calls
will return their null result values.

[standalone-test]: ../standalone-test
[fdio]: /sdk/lib/fdio
