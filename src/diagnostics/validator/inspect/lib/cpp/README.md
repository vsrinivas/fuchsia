# C++ Inspect Puppet

Reviewed on: 2019-11-12

This is the C++ Puppet for the Inspect Validator. The test
inspect\_validator\_test\_cpp checks that the C++ Inspect library conforms
to the specification.

## Building

To add this project to your build, append `--with //src/diagnostics/validator/inspect/lib/cpp:tests`
to the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with '//topaz/bundles:buildbot' --with //src/diagnostics/validator/inspect/lib/cpp:tests
```

## Testing
To run the test:
```
--with //src/diagnostics/validator/inspect/lib/cpp:tests
fx test inspect-validator-test-cpp
```
