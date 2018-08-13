# Testing

This document describes different test types used in Ledger.

[TOC]

Ledger has the following types of tests.

*** note
All of these tests run on a Fuchsia device. For the tests below we indicate the
commands to trigger the execution from the host machine, but the execution
itself happens on the target.
***

## Unit tests

**unit tests** are low-level tests written against the smallest testable parts
of the code. Tests for `some_class.{h,cc}` are placed side-by-side the code
being tested, in a `some_class_unittest.cc` file.

Unit tests are regular [Google Test] tests, although most of them use our own
[TestLoopFixture] base class to conveniently run delayed tasks with a
timeout, ensuring that a failing test does not hang forever.

All unit tests in the Ledger tree are built into a single `ledger_unittests`
binary. You can run it from the host using the `fx run-test` command, which
rebuilds the test package and pushes the new binary to device each time:

```sh
fx run-test ledger_unittests
```

You can also run this binary directly on the Fuchsia device, using the full
path to the binary in the `ledger_tests` package:
`/pkgfs/packages/ledger_tests/0/test/ledger_unittests`.

## Integration tests

**integration tests** are written against client-facing FIDL services exposed by
Ledger, although these services still run in the same process as the test code.

Integration tests inherit from [IntegrationTest] and are placed under
[/bin/ledger/tests/integration].

All integration tests in the Ledger tree are built into a single
`ledger_integration_tests` binary. You can run it from the host:

```sh
fx run-test ledger_integration_tests
```

You can also run this binary directly on the Fuchsia device, using the full
path to the binary in the `ledger_tests` package:
`/pkgfs/packages/ledger_tests/0/test/ledger_integration_tests`.

*** aside
Some of the tntegration tests emulate multiple Ledger instances synchronizing
data with each other. Through advanced build magic and [testing abstractions],
these are run both as integration tests (against fake in-memory implementation
of the cloud provider interface), and as end-to-end synchronization tests
(against a real server).
***

## End-to-end tests

**End-to-end tests** are also written against client-facing FIDL services
exposed by Ledger, but in this case the test code runs in a separate process,
and connects to Ledger the same way any other client application would do. This
is the highest-level way of testing that exercises all of the Ledger stack.

### Local tests

End-to-end tests not depending on cross-device synchronization are called "local
end-to-end tests" and are defined in [/bin/ledger/tests/e2e_local]. All local
end-to-end tests are built into a single `ledger_e2e_local` binary. You can run
it from the host:

```sh
fx run-test ledger_e2e_local
```

You can also run this binary directly on the Fuchsia device, using the full
path to the binary in the `ledger_tests` package:
`/pkgfs/packages/ledger_tests/0/test/ledger_e2e_local`.

### Synchronization tests

Synchronization end-to-end tests create multiple local Ledger instances
configured to synchronize using a cloud provider representing the same virtual
user, therefore emulating multiple devices synchronizing their Ledger data

Synchronization end-to-end tests require configuration to work against a real
server instance, see [server configuration]. After obtaining configuration
parameters, the tests can be run from host as follows:

```sh
fx shell "/pkgfs/packages/ledger_tests/0/test/disabled/ledger_e2e_sync \
  --server-id=<server-id> \
  --credentials-path=<credentials-file> \
  --api-key=<api-key>
```

[Google Test]: https://github.com/google/googletest
[TestLoopFixture]: https://fuchsia.googlesource.com/garnet/+/master/public/lib/gtest/test_loop_fixture.h
[IntegrationTest]: /bin/ledger/tests/integration/integration_test.h
[/bin/ledger/tests/integration]: /bin/ledger/tests/integration
[Synchronization end-to-end tests]: /bin/ledger/tests/e2e_sync/README.md
[/bin/ledger/tests/e2e_local]: /bin/ledger/tests/e2e_local
[/bin/ledger/tests/e2e_sync]: /bin/ledger/tests/e2e_sync
[server configuration]: /bin/cloud_provider_firestore/docs/configuration.md
[testing abstractions]: /bin/ledger/testing/ledger_app_instance_factory.h
