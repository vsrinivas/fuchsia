# Product Bundle Metadata Set (PBMS) library

The PBMS library builds upon the FMS lib and GCS lib to provide common
functionality to ffx plugins. It provides utilities to read the Product Bundle
container for an SDK. The Product Bundle container holds data for product
bundles, virtual device specification, etc.

## Development of PBMS Lib

When working on PBMS lib, consider using:

```
$ fx set [...] --with-host //src/developer/ffx/lib/pbms:tests
```

### Unit Tests

Unit tests can be run with:

```
$ fx test pbms_lib_test
```
