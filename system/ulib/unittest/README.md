# ulib/unittest

This directory contains a harness, named Unittest, for writing tests
used by system/utest.

N.B. This library cannot use fdio since system/utest/core uses it
and system/utest/core cannot use fdio. See system/utest/core/README.md.

## Rules for parsing argv

Unittest has a set of options that it recognizes.
All tests are expected to call `unittest_run_all_tests()`,
which will ensure all tests get these options.

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
