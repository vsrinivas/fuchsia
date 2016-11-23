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
- `dart_app` defines a Dart executable (coming soon™)
- [`flutter_app`](https://github.com/flutter/engine/blob/master/build/flutter_app.gni)
  defines a [Flutter](https://flutter.io/) application

See the definitions of each of these targets for how to use them.


## Managing third-party dependencies

[`third-party/dart-pkg`](https://fuchsia.googlesource.com/third_party/dart-pkg/+/master)
is kept up-to-date with
[a script](https://fuchsia.googlesource.com/scripts/+/master/update_dart_packages.py)
that relies on `pub` to resolve versions and fetch sources for the packages that
are used in the tree.
This script uses a set of canonical local packages which are assumed to be
providing the necessary package coverage for the entire tree.

Additionally, projects may request third-party dependencies to be imported
through the following procedure:
1. create a `dart_dependencies.yaml` file in the project
1. add some dependencies in that file:
```
name: my_project
dependencies:
  foo: ^4.0.0
  bar: >=0.1.0
```
1. add a reference to the file in `//scripts/update_dart_packages.py`
1. run that script


## Analysis

For each `dart_package` and `flutter_app` target, an analysis script gets
generated in the output directory under:
```sh
out/<build-type>/gen/path/to/package/package.analyzer.sh
```
Running this script will perform an analysis of the target's sources.

The `//scripts/run-dart-analysis.py` script makes it easy to run the analysis over
multiple targets:
```sh
scripts/run-dart-analysis.py --out out/<build-type> --tree //apps/sysui/*
```


## FIDL

[FIDL](https://fuchsia.googlesource.com/fidl/+/master/fidl.gni) targets generate
implicit Dart bindings targets. To use the bindings generated for:
```
//foo/bar/blah
```
add a dependency on:
```
//foo/bar/blah:blah_dart
```
and import the resulting Dart sources with:
```
import "package:foo.bar.blah/baz.dart";
```


## Known issues

#### Multiple FIDL targets in a single BUILD file

Package names for Dart bindings are inferred from the location of the source
.fidl files. Multiple FIDL targets in a same BUILD file will therefore share a
package name, which may result in issues - most notably when analyzing the
resulting Dart code. This is currently not a build issue as the generated code
for the targets is located in the same directory. This is a known issue which
will be fixed at some point.

#### Mixing FIDL targets and Dart targets

If a FIDL target coexists with another Dart target whose package name
is inferred rather than explicitly specified, the build will fail as these two
targets will attempt to map the same package name to two different source
location (respectively in the output directory and the source tree).

The current workaround is to always specify a package name for Dart targets
coexisting with FIDL targets.
