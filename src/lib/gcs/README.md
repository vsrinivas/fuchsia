# Google Cloud Storage library

The GCS library provides download support for data from Google Cloud Storage.

## Development

When working on GCS lib, consider using:

```
$ fx set [...] --with //src/lib/gcs:tests
```

### Unit Tests

Unit tests can be run with:

```
$ fx test gcs_lib_test
```

There is an interactive test executable in the "test" sub-directory. It can be
run with:

```
$ out/default/host_x64/gcs_test_client
```
(Replace "out/default" with your local build directory).
