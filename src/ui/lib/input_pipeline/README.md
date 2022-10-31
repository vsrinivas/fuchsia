# input_pipeline

Reviewed on: 2022-03-22

`input_pipeline` is a library for creating an input pipeline.
* For information on how to integrate this library with a larger Rust
  program, see [integration docs](docs/integration.md).

## Building
To add `input` to your build, append `--with //src/ui/lib/input_pipeline` to the
`fx set` invocation.

## Using
`input_pipeline` can be used by depending on the `//src/ui/lib/input_pipeline` GN target.

`input_pipeline` is not available in the SDK.

## Testing
Unit tests for `input_pipeline` are available in the `input_pipeline_lib_tests` package.

```shell
$ fx test input_pipeline_lib_tests
```

## Run-time Configuration and Debugging

### Keymap handler

Change the keymap using the following commands, for example:

```bash
ffx config set setui true // only need to run once
ffx setui keyboard --keymap UsQwerty
```

Use:

```bash
ffx setui keyboard --help
```

for more information.

## More documentation

See the [`docs` folder](docs/).
