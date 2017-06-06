# Testing

Ledger has three types of tests.

## Unit tests

**unit tests** are low-level tests written against the smallest testable parts of
the code. Tests for `some_class.{h,cc}` are placed side-by-side the code being
tested, in a `some_class_unittest.cc` file.

Unit tests are regular [Google Test] tests, although most of them use our own
[TestWithMessageLoop] base class to conveniently run delayed tasks with a
timeout, ensuring that a failing test does not hang forever.

All unit tests in the Ledger tree are built into a single `ledger_unittests`
binary, that by default can be executed on Fuchsia by running
`/system/test/ledger_unittests`.

## Integration tests

**integration tests** are written against client-facing FIDL services exposed by
Ledger, although these services still run in the same process as the test code.

Integration tests inherit from [IntegrationTest] and are placed under
`src/app/integration_tests`.

All integration tests in the Ledger tree are built into a single
`ledger_integration_tests` binary, that by default can be executed on Fuchsia by
running `/system/test/ledger_integration_tests`.

## Application tests

**application tests** are also written against client-facing FIDL services
exposed by Ledger, but in this case the test code runs in a separate process,
and connects to Ledger the same way any other client application would do. This
is the highest-level way of testing that exercises all of the Ledger stack.

Application tests inherit from [LedgerAppTest] and are currently all placed in
one file at `src/app/ledger_apptests.cc`.

All application tests in the Ledger tree are built into a single
`ledger_apptests` binary, that by default can be executed on Fuchsia by running
`ledger_apptests`.

[Google Test]: https://github.com/google/googletest
[TestWithMessageLoop]: https://fuchsia.googlesource.com/ledger/+/master/src/test/test_with_message_loop.h
[IntegrationTest]: https://fuchsia.googlesource.com/ledger/+/master/src/app/integration_tests/integration_test.h
[LedgerAppTest]: https://fuchsia.googlesource.com/ledger/+/master/src/app/ledger_apptest.cc
