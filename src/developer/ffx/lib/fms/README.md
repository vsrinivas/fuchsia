# Fuchsia Manifest System (FMS) library

The FMS library (will) support the lookup of a manifest entry based on an entry
name and downloading the artifact(s) referenced in the manifest entry.

[This lib will be a work in progress for Q2, 2021.]

## Development of FMS Lib

When working on FMS lib, consider using:

```
$ fx set [...] --with //src/developer/ffx/lib/fms:tests
```

### Unit Tests

Unit tests can be run with:

```
$ fx test fms_lib_test
```
