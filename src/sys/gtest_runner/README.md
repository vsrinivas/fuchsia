# gtest runner

Reviewed on: 2019-09-24

Gtest runner is a component runner which runs v1 gtests and integrates them with the Fuchsia Test Framework API. This is an initial implementation and still WIP.

## Building

Gtest runner should be included in all builds of Fuchsia, but if missing
can be added to builds by including `--with //src/sys/gtest_runner` to the
`fx set` invocation.

## Running

This would be eventually executed from [test executor](https://fuchsia.googlesource.com/fuchsia/+/master/garnet/bin/sl4f/src/test).

## Testing

Unit tests for gtest runner are available in the `gtest_runner_tests`
package.

```
$ fx run-test gtest_runner_tests
```