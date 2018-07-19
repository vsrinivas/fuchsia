# Testing

Ledger has three types of tests.

## Unit tests

**unit tests** are low-level tests written against the smallest testable parts of
the code. Tests for `some_class.{h,cc}` are placed side-by-side the code being
tested, in a `some_class_unittest.cc` file.

Unit tests are regular [Google Test] tests, although most of them use our own
[TestLoopFixture] base class to conveniently run delayed tasks with a
timeout, ensuring that a failing test does not hang forever.

All unit tests in the Ledger tree are built into a single `ledger_unittests`
binary, that by default can be executed on Fuchsia by running
`/pkgfs/packages/ledger_tests/0/test/ledger_unittests`.

## Integration tests

**integration tests** are written against client-facing FIDL services exposed by
Ledger, although these services still run in the same process as the test code.

Integration tests inherit from [IntegrationTest] and are placed under
[/bin/ledger/tests/integration].

All integration tests in the Ledger tree are built into a single
`ledger_integration_tests` binary, that by default can be executed on Fuchsia by
running `/pkgfs/packages/ledger_tests/0/test/ledger_integration_tests`. Some
end-to-end tests (synchronization tests, see below) are also run as integration
tests using fake implementations of cloud services.

## End-to-end tests

**End-to-end tests** are also written against client-facing FIDL services
exposed by Ledger, but in this case the test code runs in a separate process,
and connects to Ledger the same way any other client application would do. This
is the highest-level way of testing that exercises all of the Ledger stack.

End-to-end tests exercising only the local part of the Ledger are in
[/bin/ledger/tests/e2e_local]. [Synchronization end-to-end tests] exercising
multi-device synchronization are in [/bin/ledger/tests/e2e_sync] and compiled
both as end-to-end tests exercising real cloud services, and as integration
tests using fake services.

All local application tests in the Ledger tree are built into a single
`ledger_e2e_local` binary, that by default can be executed on Fuchsia by running
`/pkgfs/packages/ledger_tests/0/test/ledger_e2e_local`. Synchronization tests
are built into a single `ledger_e2e_sync` binary, that by default can be
executed on Fuchsia by running
`/pkgfs/packages/ledger_tests/0/test/disabled/ledger_e2e_sync`.

[Google Test]: https://github.com/google/googletest
[TestLoopFixture]: https://fuchsia.googlesource.com/garnet/+/master/public/lib/gtest/test_loop_fixture.h
[IntegrationTest]: /bin/ledger/tests/integration/integration_test.h
[/bin/ledger/tests/integration]: /bin/ledger/tests/integration
[Synchronization end-to-end tests]: /bin/ledger/tests/e2e_sync/README.md
[/bin/ledger/tests/e2e_local]: /bin/ledger/tests/e2e_local
[/bin/ledger/tests/e2e_sync]: /bin/ledger/tests/e2e_sync
