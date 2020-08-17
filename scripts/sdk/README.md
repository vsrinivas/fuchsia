SDK frontends
=============

This directory contains frontends to the SDK pipeline:
- [`bazel/`](bazel): creates a C/C++/Dart/Flutter Bazel workspace. **For
  internal use only.**
- [`gn/`](gn): creates a C/C++ GN workspace.

In addition, the `common/` directory provides plumbing shared by all frontends,
and `tools/` contains various tools to work with SDK manifests.
