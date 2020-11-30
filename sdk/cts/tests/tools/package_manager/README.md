# package_manager_test README

### What is this test about?

This is a set of tests for the package management workflow. It covers
commands done using `pm`, `amberctl`, and `pkgctl`.

In all, the tests cover the flow spanning from `*.far` and repo
creation to running binaries on-device from the served `*.far`.

See [packages](https://fuchsia.dev/fuchsia-src/development/sdk/documentation/packages)
for more documentation.

### What are the corresponding smaller tests?

The individual tests can be found in `test/package_manager_test.dart`.

Relevant code paths with unit and integration tests:

```
//src/sys/pkg/tests/amberctl
//src/sys/pkg/tests/pkgctl
//src/sys/pkg/testing/host-target-testing/packages
```

### What unique coverage does this E2E test provide?

These tests differ from the pre-existing tests for package management
because these run the command-line binaries themselves while the
other tests programmatically test the functions and libraries that
comprise those binaries.

They are also primarily testing the actual commands (with the same
flags) that consumers were found using, so these tests are ensuring
we don't break our consumers.
