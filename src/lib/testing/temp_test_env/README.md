# TempTestEnv

The TempTestEnv library helps with host testing. A temporary environment is set
up to isolate tests which work with the process global environment. This is for
testing convenience *only*, it is not a secure sandbox by any means.

## Development

When working on TempTestEnv lib, consider using:

```
$ fx set [...] --with //src/lib:tests
```

### Unit Tests

Unit tests can be run with:

```
$ fx test temp_test_env_lib_test
```
