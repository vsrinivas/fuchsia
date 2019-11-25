# Validation Test Suite for cloud providers

This directory contains implementation-independent tests for implementations of
the [CloudProvider] interface.

In order to run the tests, run the `cloud_provider_validation_tests` binary with
the cloud provider implementation exposed to it via the `svc` directory.

We expect that individual cloud provider implementations would develop custom
launcher applications that configure and run the validation tests against their
implementation of CloudProvider ([example]).

[CloudProvider]: /peridot/public/fidl/fuchsia.ledger.cloud/cloud_provider.fidl
[example]: /src/ledger/cloud_provider_in_memory/bin/validation/
