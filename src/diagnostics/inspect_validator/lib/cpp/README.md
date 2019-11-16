# C++ Inspect Puppet

Reviewed on: 2019-11-12

This is the C++ Puppet for the Inspect Validator. The test
inspect\_validator\_test\_cpp checks that the C++ Inspect library conforms
to the specification.

## Building

This project can be added to builds by including `--with //src/diagnostics/inspect_validator/lib/cpp:tests`
to the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with '//topaz/bundles:buildbot' --with //src/diagnostics/inspect_validator/lib/cpp:tests
```

## Testing
To run the test:
```
--with //src/diagnostics/inspect_validator/lib/cpp:tests
fx run-test inspect_validator_cpp_tests
```
