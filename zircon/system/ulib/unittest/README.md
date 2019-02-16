# ulib/unittest

This directory contains a harness, named Unittest, for writing tests
used by system/utest.

N.B. This library cannot use fdio since system/utest/core uses it
and system/utest/core cannot use fdio. See system/utest/core/README.md.

**If you want your unit tests to print to standard out, you must link your
test executable to system/ulib/fdio!**

## Rules for use of printf vs unittest_printf

Tests are expected to *not* call `printf()`. By default we want tests
to be less verbose, and use of printfs about means the output cannot
not be disabled. This test harness can call `printf()`, but tests should not.
Instead tests are expected to either call `unittest_printf()` or
`unitest_printf_critical()`, the difference being that the former is
controlled by the verbosity level, and the latter is not.
Obviously `unittest_printf_critical()` is intended to be used *sparingly*.

## Test verbosity

Verbosity is controlled by passing `v=N` when invoking the test,
where N has the range 0-9. The default is zero, which means
`unittest_printf()` output does not appear. Any value above zero enables
`unittest_printf()` output.

## Rules for parsing argv

Unittest has a set of options that it recognizes.
All tests are expected to call `unittest_run_all_tests()`,
which will ensure all tests get these options.
The library supplies a default `main()` function that does this.
So if a test has no interest in the arguments itself and needs no
other special global initialization, it need not define its own `main` at all.

However, tests can also have their own options. Since Unittest does not
use any kind of general argv parsing library, and each test as well as
Unittest do their own parsing, one issue is how to support both Unittest
options and test-specific options without either having to know about the
other. This becomes important when parsing an option that takes a value and
the value might begin with "-". E.g.,

```
$ foo-test --foo -f -f bar
```

where the first `-f` is the value for option `--foo`
and the second `-f` is an option specific to `foo-test`.

Argv processing is first done in the `main()` of the testcase, and
then again in Unittest when the testcase calls `unittest_run_all_tests()`.
If `--foo` is a Unittest option, how does the testcase know to
ignore the first `-f`? The solution we employ is very simple:

*Parse argv one element at a time,*
*and ignore anything that is not recognized.*

This simple rule makes writing tests easy, but it does have some consequences
one needs to be aware of. For example, this means that option values cannot
begin with "-", which makes the above example invalid.
A second consequence is that there are no positional parameters.
E.g.,

```
$ foo-test --foo ./-f a b c -f bar
```
is equivalent to
```
$ foo-test --foo ./-f -f bar a b c
```

While not entirely clean, this allows for a simple implementation,
and preserves the status quo.
