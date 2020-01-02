# `runner`

Reviewed on: 2019-12-30

`runner` is a library which can be used by various runner to launch components.
It provides basic utility APIs.

## Building

This project can be added to builds by including `--with //src/sys/lib/runner`
to the `fx set` invocation.

## Using

`runner` can be used by depending on the `//src/sys/lib/runner` GN target and
then using the `runner` crate in a rust project.

`runner` is not available in the SDK.

## Testing

Unit tests for `runner` are available in the `runner_tests` package.

```
$ fx run-test runner_tests
```
