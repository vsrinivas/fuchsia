# input_pipeline

Reviewed on: 2021-05-07

`input_pipeline` is a library for creating an input pipeline. For more information, 
see [Input client library](/docs/concepts/session/input.md).

## Building
To add `input` to your build, append `--with //src/ui/lib/input_pipeline` to the
`fx set` invocation.

## Using
`input_pipeline` can be used by depending on the `//src/ui/lib/input_pipeline` GN target.

`input_pipeline` is not available in the SDK.

## Testing
Unit tests for `input_pipeline` are available in the `input_pipeline_lib_tests` package.

```shell
$ fx test input_pipeline_tests
```

## Source layout
The main implementation is linked in `src/lib.rs`.

## Implementation notes

### Keymap handler

Change the keymap using the following commands.  Only two keymaps are supported
at the moment.  This will change.

```
fx ffx session keyboard --keymap FR_AZERTY
fx ffx session keyboard --keymap US_QWERTY
```
