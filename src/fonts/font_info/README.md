# `font_info`

A Rust library for reading information from font files. Can be built for either
Fuchsia or host targets.

On Fuchsia, supports both reading from both local paths and VMOs. On host, only
supports local paths.

## Testing

Make sure your `fx set` arguments include `--with //bundles:tests`.

### Fuchsia
```shell
fx test font_info_tests
```

## Host
```shell
fx run-host-tests font_info_test
```

Note the absence of final `s` in `font_info_test`.
