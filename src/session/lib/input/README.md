# input

Reviewed on: 2021-01-21

`input` is a library for adding an input pipeline to sessions. For more information, see [Input client library](/docs/concepts/session/input.md).

## Building
To add `input` to your build, append `--with //src/session/lib/input` to the `fx set` invocation.

## Using
`input` can be used by depending on the `//src/session/lib/input` GN target.

`input` is not available in the SDK.

## Testing
Unit tests for `input` are available in the `input_tests` package.

```shell
$ fx test input_tests
```

## Source layout
The main implementation is linked in `src/lib.rs`.