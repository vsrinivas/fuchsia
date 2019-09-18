# Golden tests for the wisdom example

This directory contains the tests that compare the response printed out by
the polyglot wisdom tests with the contents of `golden-output.txt`.

Since this is a component test, all it has to do to run polyglot tests is to
start a component from a different URL.

# Building

A prerequisite for building the tests is having the correct `fx set` command.

You can add the flag: `--with=//garnet/examples/intl/wisdom/test:tests` to include
all the needed tests.

# Running the tests

Once done, the following sequence will run the test:

```
fx build
fx serve -v  # You want to run this synchronously in a different terminal.
fx run-test intl_wisdom_test
```

