# `by_addr`

Reviewed on: 2019-09-24

`by_addr` is a library which a wrapper class for `std::sync::Arc` and gives a way to compare two Arcs using memory address.

## Building

To add this project to your build, append `--with
//src/sys/lib/by_addr` to the `fx set` invocation.

## Using

`by_addr` can be used by depending on the `//src/sys/lib/by_addr`
GN target and then using the `by_addr` crate in a rust project.

`by_addr` is not available in the SDK.

## Testing

Unit tests for `by_addr` are available in the `by_addr_tests` package.

```
$ fx run-test by_addr_tests
```
