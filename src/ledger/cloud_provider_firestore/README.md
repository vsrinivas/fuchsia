# Firestore-based cloud provider

This directory contains a Firestore-based implementation of the [cloud provider]
interface.

[TOC]

## Testing

The tests are packaged with Ledger tests.

In order to run the unit tests:

```sh
fx run-test ledger_tests -t cloud_provider_firestore_unittests
```

In order to run the [validation tests], follow the [cloud sync set-up
instructions] to set up a Firestore instance, configure the build environment
and obtain the sync parameters.

Then, run the validation tests as follows:

```sh
fx push-package ledger_tests
fx shell run-test-component validation_firestore
```

Note that `validation_firestore` is only a launcher for the actual tests,
`cloud_provider_validation_tests`. As a result, you will need to look at `fx
log` output to see if the tests passed.

## Documentation

 - [configuration](docs/configuration.md) of server instances

[cloud provider]: /peridot/public/fidl/fuchsia.ledger.cloud/cloud_provider.fidl
[cloud sync set-up instructions]: /src/ledger/docs/testing.md#cloud-sync
[validation tests]: /peridot/public/lib/cloud_provider/validation/README.md
