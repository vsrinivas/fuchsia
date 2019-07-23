# `fuchsia_url`

Reviewed on: 2019-07-12

`fuchsia_url` is a library for parsing and constructing fuchsia boot and fuchsia
package URLs. Docs are available
[here](https://fuchsia-docs.firebaseapp.com/rust/fuchsia_url/index.html).

## Building

This project can be added to builds by including `--with
//src/sys/lib/fuchsia_url` to the `fx set` invocation.

## Using

`fuchsia_url` can be used by depending on the `//src/sys/lib/fuchsia_url`
GN target and then using the `fuchsia_url` crate in a rust project.

`fuchsia_url` is not available in the SDK.

## Testing

Unit tests for `fuchsia_url` are available in the `fuchsia_url_tests` package.

```
$ fx run-test fuchsia_url_tests
```
