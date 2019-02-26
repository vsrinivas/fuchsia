# Testing

This document describes different test types used in Ledger.

*** note
All of the tests run on a Fuchsia device (real or emulated). For the tests below
we indicate the commands to trigger the execution from the host machine, but the
execution itself happens on the target.
***

[TOC]

## TL;DR

```sh
fx run-test ledger_tests
```

This command will run the following tests, that are enabled by default:

* [unit tests](#unit-tests)
* [integration tests](#integration-tests)
* [local end-to-end tests](#local-e2e)
* [Firestore cloud provider] unit tests

It will not run the following tests, that need to be configured and executed
manually:

* [synchronization end-to-end tests](#sync-e2e)
* [performance tests][benchmarks]
* [fuzz tests](#fuzz-tests)
* [Firestore cloud provider] end-to-end tests

## Unit tests

**Unit tests** are low-level tests written against the smallest testable parts
of the code. Tests for `some_class.{h,cc}` are placed side-by-side the code
being tested, in a `some_class_unittest.cc` file.

Unit tests are regular [Google Test] tests. Most of them use our own
[TestLoopFixture] base class to conveniently run them with a fake clock, and
simulated multi-thread, in order to know when nothing will ever happen again.

All unit tests in the Ledger tree are built into a single `ledger_unittests`
binary. The binary is self-contained: it contains both the tests and the Ledger
logic under test linked into a single Google Test binary.

You can run it from the host with:

```sh
fx run-test ledger_tests -t ledger_unittests
```

## Integration tests

**Integration tests** are written against client-facing FIDL services exposed by
Ledger, although these services still run in the same process as the test code.

Integration tests inherit from [IntegrationTest] and are placed under
[/bin/ledger/tests/integration].

All integration tests in the Ledger tree are built into a single
`ledger_integration_tests` binary. You can run it from the host:

```sh
fx run-test ledger_tests -t ledger_integration_tests
```

*** aside
Some of the integration tests emulate multiple Ledger instances synchronizing
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

### Local tests {#local-e2e}

End-to-end tests not depending on cross-device synchronization are called "local
end-to-end tests" and are defined in [/bin/ledger/tests/e2e_local]. All local
end-to-end tests are built into a single `ledger_e2e_local` binary. You can run
it from the host:

```sh
fx run-test ledger_tests -t ledger_e2e_local
```

### Synchronization tests {#sync-e2e}

Synchronization end-to-end tests create multiple local Ledger instances
configured to synchronize using a cloud provider backed by a real server. The
cloud provider instances are set up to represent the same virtual user,
therefore emulating multiple devices synchronizing their Ledger data through the
real server.

*** note
Those tests exercise cloud sync against a real server. They are not run by
default, and you need to follow the instructions below to set up server access
and run them manually.
***

<a name="cloud-sync"></a>
First, ensure you have an instance of the server configured and take note of the
configuration params (service account credentials file and API key) - in order
to obtain those, follow the [server configuration] instructions.

Then, create a json sync credentials file file of the following format:
```javascript
{
  "api-key": "<API_KEY>",
  "service-account": <SERVICE_ACCOUNT_FILE_CONTENT>
}
```
where <API_KEY> is the api key retrieved from the firebase console, and
<SERVICE_ACCOUNT_FILE_CONTENT> is the content of the service account credentials
file.

Then, put the sync credentials file whenever you like, and set the full path to
it in the GN variable:

```sh
fx set x64 --args ledger_sync_credentials_file=\"/full/path/to/sync_credentials.json\"
```

After rebuilding, the credentials file will be automatically embedded in the
relevant sync test binaries. You will still need to pass the remaining
parameters (server ID and API key) as command line parameters.

You can now run the tests from the host as follows:

```sh
fx push-package ledger_tests
fx shell run-test-component ledger_e2e_sync
```

## Performance tests

For performance tests, see [benchmarks].

## Fuzz tests

Ledger uses LibFuzzer for fuzz tests. We have a single fuzz package called
`ledger_fuzzers`. It can be built as follows:

```sh
fx set x64 --monolith peridot/packages/tests/ledger --fuzz-with asan
fx full-build
```

And then run:

```sh
fx fuzz start ledger
```

You can refer to the full [fuzzing] instructions for details.

## See also

 - [Firestore cloud provider] has its own suite of tests, including unit tests
   and end-to-end validation tests

[Google Test]: https://github.com/google/googletest
[TestLoopFixture]: https://fuchsia.googlesource.com/fuchsia/+/master/garnet/public/lib/gtest/test_loop_fixture.h
[IntegrationTest]: /src/ledger/bin/tests/integration/integration_test.h
[/bin/ledger/tests/integration]: /src/ledger/bin/tests/integration
[Synchronization end-to-end tests]: /src/ledger/bin/tests/e2e_sync/README.md
[/bin/ledger/tests/e2e_local]: /src/ledger/bin/tests/e2e_local
[/bin/ledger/tests/e2e_sync]: /src/ledger/bin/tests/e2e_sync
[server configuration]: /src/ledger/cloud_provider_firestore/docs/configuration.md
[testing abstractions]: /src/ledger/bin/testing/ledger_app_instance_factory.h
[benchmarks]: /src/ledger/bin/tests/benchmark/README.md
[Firestore cloud provider]: /src/ledger/cloud_provider_firestore/README.md#testing
[fuzzing]: https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/workflows/libfuzzer.md
