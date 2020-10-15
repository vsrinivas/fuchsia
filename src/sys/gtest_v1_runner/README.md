# gtest runner

Reviewed on: 2019-09-24

Gtest runner is a component runner which runs gtests under appmgr (components v1) and integrates
them with the Test Runner Framework API. This is an initial implementation and still WIP.

## Building

Gtest runner should be included in all builds of Fuchsia, but if missing
can be added to builds by including `--with //src/sys/gtest_v1_runner` to the
`fx set` invocation.

## Running

This would be eventually executed from [test executor](https://fuchsia.googlesource.com/fuchsia/+/master/src/testing/sl4f/src/test).

## Testing

Unit tests for gtest runner are available in the `gtest_v1_runner_tests`
package.

```
$ fx run-test gtest_v1_runner_tests
```
