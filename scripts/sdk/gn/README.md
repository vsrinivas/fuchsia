# GN SDK

The GN SDK frontend produces a [GN](https://gn.googlesource.com/gn/+/refs/heads/master/README.md) workspace.

## Directory structure

- `generate.py`: the script that generates the SDK;
- `templates`: Mako templates used to produce various SDK files;
- `base`: SDK contents that are copied verbatim;

## Generating

In order to generate the GN SDK, point the `generate.py` script to an
SDK archive, e.g.:
```
$ scripts/sdk/gn/generate.py \
    --archive my_sdk_archive.tar.gz \
    --output gn_sdk_dir/
```

### Getting the latest Core SDK:

```shell
$ BUILD_ID="$(gsutil cat gs://fuchsia/development/LATEST_LINUX)" && gsutil cp gs://fuchsia/development/$BUILD_ID/sdk/linux-amd64/core.tar.gz .
```

