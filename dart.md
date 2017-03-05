Dart
====


## Overview

Dart artifacts are not built the same way in Fuchsia as they are on other
platforms.

Instead of relying on [`pub`][pub] to manage dependencies, sources of
third-party packages we depend on are checked into the tree under
[`//third_party/dart-pkg`][dart-3p].
This is to ensure we use consistent versions of our dependencies across multiple
builds.

Likewise, no build output is placed in the source tree as everything goes under
`out/`.


## Targets

There are four gn targets for building Dart:
- [`dart_package`][target-package] defines a library that can be used by other
Dart targets;
- [`dart_app`][target-app] defines a Dart executable;
- [`flutter_app`][target-flutter] defines a [Flutter][flutter] application;
- [`dart_test`][target-test] defines a group of test.

See the definitions of each of these targets for how to use them.


## Managing third-party dependencies

[`//third-party/dart-pkg`][dart-3p] is kept up-to-date with
[a script][dart-3p-script] that relies on `pub` to resolve versions and fetch
sources for the packages that are used in the tree.
This script uses a set of canonical local packages which are assumed to be
providing the necessary package coverage for the entire tree.

Additionally, projects may request third-party dependencies to be imported
through the following procedure:
1. create a `dart_dependencies.yaml` file in the project
2. add the desired dependencies in that file:
```
name: my_project
dependencies:
  foo: ^4.0.0
  bar: >=0.1.0
```
3. add a reference to the file in `//scripts/update_dart_packages.py`
4. run that script


## Analysis

For each `dart_package` and `flutter_app` target, an analysis script gets
generated in the output directory under:
```sh
out/<build-type>/gen/path/to/package/package.analyzer.sh
```
Running this script will perform an analysis of the target's sources.

Analysis options for a given target may be set on the target itself in BUILD.gn:
```
dart_package("foo") {
  analysis_options = "//path/to/my/.analysis_options"
}
```

Analysis may likewise be disabled altogether with:
```
dart_package("foo") {
  disable_analysis = true
}
```

The `//scripts/run-dart-analysis.py` script makes it easy to run the analysis over
multiple targets:
```sh
scripts/run-dart-analysis.py --out out/<build-type> --tree //apps/sysui/*
```

Regular analyzer flags may also be passed:
```sh
scripts/run-dart-analysis.py --out out/<build-type> --fatal-warnings --lints
```
This holds true for the individual analysis scripts.


## Testing

For now `dart_test` targets are not operational. They do however allow test code
to be analyzed.


## FIDL

[FIDL][fidl] targets generate implicit Dart bindings targets. To use the
bindings generated for:
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


## IDEs

Most IDEs need to find a `.packages` file at the package root in order to be
able to resolve packages. On Fuchsia, those files are placed in the `out/`
directory. As a temporary workaround, run `//scripts/symlink-dot-packages.py` to
create symlinks in the source tree:
```
scripts/symlink-dot-packages.py --tree //apps/sysui/*
```
You may also want to update your project's `.gitignore` file to ignore these
symlinks.

### Atom

The Dart SDK built in the Fuchsia tree has a version of the analysis service
which understands the structure of the Fuchsia directory. In the `dartlang` Atom
plugin, set the Dart SDK as `out/<build-type>/<host-flavor>/dart-sdk` (e.g.
`out/debug-x86-64/host_x64/dart-sdk`). Packages and apps with build targets
should now be error-free.


## Known issues

#### Multiple FIDL targets in a single BUILD file

If two FIDL targets coexist in a single BUILD file, their respective, generated
files will currently be placed in the same subdirectory of the output directory.
This means that files belonging to one target will be available to clients of
the other target, and this will likely confuse the analyzer.
This should not be a build issue now but could become one once the generated
Dart files are placed in separate directories if clients do not correctly set up
their dependencies.

#### Location of `dart_test` targets

The current implementation of `dart_package` forces test targets to be defined
in their own build file. The best location for that file is the test directory
itself.


[pub]: https://www.dartlang.org/tools/pub/get-started "Pub"
[dart-3p]: https://fuchsia.googlesource.com/third_party/dart-pkg/+/master "Third-party dependencies"
[dart-3p-script]: https://fuchsia.googlesource.com/scripts/+/master/update_dart_packages.py "Dependencies script"
[target-package]: https://fuchsia.googlesource.com/build/+/master/dart/dart_package.gni "dart_package target"
[target-app]: https://fuchsia.googlesource.com/dart_content_handler/+/master/dart_app.gni "dart_package app"
[target-flutter]: https://github.com/flutter/engine/blob/master/build/flutter_app.gni "flutter_app target"
[target-test]: https://fuchsia.googlesource.com/build/+/master/dart/dart_test.gni "dart_test target"
[flutter]: https://flutter.io/ "Flutter"
[fidl]: https://fuchsia.googlesource.com/fidl/+/master/fidl.gni "FIDL"
