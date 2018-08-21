# Firestore-based cloud provider

This directory contains a Firestore-based implementation of the [cloud provider]
interface.

[TOC]

## Testing

In order to run the unit tests:

```sh
fx run-test cloud_provider_firestore_unittests
```

In order to run the [validation tests], follow the [cloud sync set-up
instructions] to set up a Firestore instance, configure the build environment
and obtain the sync parameters needed below.

Then, run the validation tests as follows:

```sh
fx shell "run fuchsia-pkg://fuchsia.com/ledger_tests#meta/validation_firestore.cmx \
  --server-id=<server-id> \
  --api-key=<api-key>
```


## Documentation

 - [configuration](docs/configuration.md) of server instances

[cloud provider]: /public/fidl/fuchsia.ledger.cloud/cloud_provider.fidl
[cloud sync set-up instructions]: /docs/ledger/testing.md#cloud-sync
[validation tests]: /public/lib/cloud_provider/validation/README.md
