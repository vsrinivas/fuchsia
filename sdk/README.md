SDK frontends
=============

This directory contains frontends to the SDK pipeline:
- `bazel/`: creates a C/C++ workspace;
- `dart-pub/`: creates a Dart SDK that integrates with a pub environment;
- `foundation/`: creates a low-level C/C++ SDK.

In addition, the `common/` directory provides plumbing shared by all frontends,
and `tools/` contains various tools to work with SDK manifests.
