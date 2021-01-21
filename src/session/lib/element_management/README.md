# element_management

Reviewed on: 2021-01-21

`element_management` is a library for adding element managers to sessions. For the related
documentation issue, see [fxbug.dev/43907](fxbug.dev/43907).

## Building
To add `element_management` to your build, append `--with //src/session/lib/element_management` to
the `fx set` invocation.

## Using
`element_management` can be used by depending on the `//src/session/lib/element_management`
GN target.

`element_management` is not available in the SDK.

## Testing
Unit tests for `element_management` are available in the `element_management_tests` package.

```shell
$ fx test element_management_tests
```

## Source layout
The implementation is in `src/lib.rs`, which also includes unit tests.