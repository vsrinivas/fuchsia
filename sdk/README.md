SDK frontends
=============

This directory contains frontends to the SDK pipeline:
- `bazel/`: creates a C/C++ workspace;
- `dart-pub/`: creates a Dart SDK that integrates with a pub environment;
- `foundation/`: creates a low-level C/C++ SDK.

In addition, the `common/` directory provides plumbing shared by all frontends,
and `tools/` contains various tools to work with SDK manifests.

## Generating

To generate an SDK, first build an SDK package, for example:

```
$ fx set x64 out/sdk-x64 --packages topaz/packages/sdk/topaz
$ fx full-build
```

Then run the `generate.py` script against the SDK manifest, for example:

```
./scripts/sdk/bazel/generate.py --manifest out/sdk-x64/sdk-manifests/topaz --output /tmp/bazel_sdk
```
