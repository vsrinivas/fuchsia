# `library_loader`

Reviewed on: 2019-09-24

`library_loader` is a library to load executable VMOs along with dependent libraries.

## Building

To add this project to your build, append `--with
//src/sys/lib/library_loader` to the `fx set` invocation.

## Using

`library_loader` can be used by depending on the `//src/sys/lib/library_loader`
GN target and then using the `library_loader` crate in a rust project.

`library_loader` is not available in the SDK.

## Testing

Unit tests for `library_loader` are available in the `library_loader_tests` package.

```
$ fx run-test library_loader_tests
```
