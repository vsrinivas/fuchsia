SDK frontends
=============

This directory contains frontends to the SDK pipeline:
- [`bazel/`](bazel): creates a C/C++/Dart/Flutter Bazel workspace;
- [`dart-pub/`](dart-pub): creates a Dart SDK that integrates with a pub environment.

In addition, the `common/` directory provides plumbing shared by all frontends,
and `tools/` contains various tools to work with SDK manifests.
