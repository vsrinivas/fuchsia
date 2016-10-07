Dart
====


## Overview

Dart artifacts are not built the same way in Fuchsia as they are on other
platforms.

Instead of relying on
[`pub`](https://www.dartlang.org/tools/pub/get-started) to manage dependencies,
sources of third-party packages we depend on are checked into the tree under
[`third_party/dart-pkg`](https://fuchsia.googlesource.com/third_party/dart-pkg/+/master).
This is to ensure we use consistent versions of our dependencies across multiple
builds.


## Targets

There are three gn targets for building Dart:
- [`dart_package`](https://fuchsia.googlesource.com/build/+/master/dart/dart_package.gni)
  defines a library that can be used by other Dart targets
- `dart_app` defines a Dart executable, coming soon!
- [`flutter_app`](https://github.com/flutter/engine/blob/master/build/flutter_app.gni)
  defines a [Flutter](https://flutter.io/) application.

See the definitions of each of these targets for how to use them.


## Managing third-party dependencies

[`third-party/dart-pkg`](https://fuchsia.googlesource.com/third_party/dart-pkg/+/master)
is kept up-to-date with
[a script](https://fuchsia.googlesource.com/scripts/+/master/update_dart_packages.py)
that relies on `pub` to resolve versions and fetch sources for the packages that
are used in the tree.
This script uses a set of canonical local packages which are assumed to be
providing the necessary package coverage for the entire tree. Eventually a
mechanism will be added to allow projects to specify which packages they
require.
