# ICU data extractor

This a host tool that can extract specified data from ICU's data files.

The built binary is found at `out/default/host_x64/icu_data_extractor`.

## Building

```shell
$ fx build host-tools/icu_data_extractor
```

TODO(kpozin): The above doesn't work. GN doesn't recognize the toolchain suffix.

## Running
For usage instructions and available commands, run

```shell
$ host-tools/icu_data_extractor --help
```