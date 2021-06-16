# `fuchsia-url`

Reviewed on: 2020-06-15

`fuchsia-url` is a library for parsing and constructing fuchsia boot and fuchsia
package URLs. Docs are available
[here](https://fuchsia.dev/fuchsia-src/concepts/packages/package_url).

## Building

To add this project to your build, append `--with //src/lib/fuchsia-url`
to the `fx set` invocation.

## Using

`fuchsia-url` can be used by depending on the `//src/lib/fuchsia-url`
GN target and then using the `fuchsia_url` crate in a rust project.

`fuchsia-url` is not available in the SDK.

## Testing

Unit tests for `fuchsia-url` are available in the `fuchsia-url-tests` package.

```
$ fx test fuchsia-url-tests
```
