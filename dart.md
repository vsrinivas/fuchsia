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
`out/`. That includes `.packages` files which are generated as part of the build
based on a target's dependency.


## Targets

There are five gn targets for building Dart:
- [`dart_package`][target-package] defines a library that can be used by other
Dart targets;
- [`dart_app`][target-app] defines a Dart executable for Fuchsia;
- [`dart_tool`][target-tool] defines a Dart tool for the host;
- [`flutter_app`][target-flutter] defines a [Flutter][flutter] application;
- [`dart_test`][target-test] defines a group of test.

See the definitions of each of these targets for how to use them.

## Package layout

We use a layout very similar to the [standard layout][package-layout].

```
my_package/
  |
  |-- pubspec.yaml       # Empty, used as a marker
  |-- BUILD.gn           # Contains all targets
  |-- .analysis_options  # Note: can be moved to a central location
  |-- lib/               # dart_package contents
  |-- bin/               # dart_binary's (target) or dart_tool's (host)
  |-- test/              # dart_test contents
```


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

The `//scripts/run-dart-action.py` script makes it easy to run the analysis over
multiple targets:
```sh
scripts/run-dart-action.py analyze --out out/<build-type> --tree //apps/sysui/*
```

Regular analyzer flags may also be passed:
```sh
scripts/run-dart-action.py analyze --out out/<build-type> --fatal-warnings --lints
```
This holds true for the individual analysis scripts.

Lastly, it is possible to automatically run analysis as part of the build by
passing the `--with-dart-analysis` flag to the `gen.py` script.


## Testing

The `dart_test` target is appropriate for unit tests.
Each target yields a test script in the output directory under:
```sh
out/<build-type>/gen/path/to/package/target_name.sh
```
This script simply runs the given tests in the Flutter shell *on the host*.

The `//scripts/run-dart-action.py` script may be used to run multiple test
suites at once:
```sh
scripts/run-dart-action.py test --out out/<build-type> --tree //apps/sysui/*
```
It also works with a single suite:
```sh
scripts/run-dart-action.py test --out out/<build-type> --tree //apps/sysui/armadillo:test
```


## FIDL

[FIDL][fidl] targets generate implicit Dart bindings targets. To use the
bindings generated for:
```
//foo/bar
//foo/bar:blah
```
add a dependency on:
```
//foo/bar:bar_dart
//foo/bar:blah_dart
```
and import the resulting Dart sources with:
```
import "package:foo.bar/baz.dart";
import "package:foo.bar..blah/baz.dart";
```


## Logging

It is highly recommended that you use `lib.logging` package when you want to add
logging statements to your Dart package.

Include the `lib.logging` package in your BUILD.gn target as a dependency:
```
deps = [
  ...
  "//topaz/lib/widgets/packages/logging:lib.logging",
  ...
]
```

In the main function of your Dart / Flutter app, call the `setupLogger()`
function to make sure logs appear in the Fuchsia console in the desired format.
```dart
import 'package:lib.logging/logging.dart';

main() {
  setupLogger();
}
```

After setting this up, you can call one of the following log methods to add log
statements to your code:
```dart
import 'package:lib.logging/logging.dart';

// add logging statements somewhere in your code as follows:
log.info('hello world!');
```

The `log` object is a `Logger` instance as documented [here][logger-doc].


### Log Levels

The log methods are named after the supported log levels. To list the log
methods in descending order of severity:
```dart
log.shout()    // maps to LOG_FATAL in FXL.
log.severe()   // maps to LOG_ERROR in FXL.
log.warning()  // maps to LOG_WARNING in FXL.
log.info()     // maps to LOG_INFO in FXL.
log.fine()     // maps to VLOG(1) in FXL.
log.finer()    // maps to VLOG(2) in FXL.
log.finest()   // maps to VLOG(3) in FXL.
```

By default, all the logs of which level is INFO or higher will be shown in the
console. Because of this, Dart / Flutter app developers are highly encouraged to
use `log.fine()` for their typical logging statements for development purposes.

Currently, the log level should be adjusted in individual Dart apps by providing
the `level` parameter in the `setupLogger()` call. For example:
```dart
setupLogger(level: Level.ALL);
```
will make all log statements appear in the console.


## IDEs

### Atom

The Dart SDK built in the Fuchsia tree has a version of the analysis service
which understands the structure of the Fuchsia directory. In the `dartlang` Atom
plugin, set the Dart SDK as `//dart/tools/sdks/<linux|mac>/dart-sdk`. Packages
and apps with build targets should now be error-free.
Note that in order for the plugin to locate a Dart package, it needs a marker:
add an empty `pubspec.yaml` file at the root of the package.

### Others

Most IDEs need to find a `.packages` file at the package root in order to be
able to resolve packages. On Fuchsia, those files are placed in the `out/`
directory. As a temporary workaround, run `//scripts/symlink-dot-packages.py` to
create symlinks in the source tree:
```
scripts/symlink-dot-packages.py --tree //apps/sysui/*
```
You may also want to update your project's `.gitignore` file to ignore these
symlinks.


## Known issues

### Multiple FIDL targets in a single BUILD file

If two FIDL targets coexist in a single BUILD file, their respective, generated
files will currently be placed in the same subdirectory of the output directory.
This means that files belonging to one target will be available to clients of
the other target, and this will likely confuse the analyzer.
This should not be a build issue now but could become one once the generated
Dart files are placed in separate directories if clients do not correctly set up
their dependencies.


[pub]: https://www.dartlang.org/tools/pub/get-started "Pub"
[dart-3p]: https://fuchsia.googlesource.com/third_party/dart-pkg/+/master "Third-party dependencies"
[dart-3p-script]: https://fuchsia.googlesource.com/scripts/+/master/update_dart_packages.py "Dependencies script"
[package-layout]: https://www.dartlang.org/tools/pub/package-layout "Package layout"
[target-package]: https://fuchsia.googlesource.com/build/+/master/dart/dart_package.gni "dart_package target"
[target-app]: https://fuchsia.googlesource.com/dart_content_handler/+/master/dart_app.gni "dart_package app"
[target-tool]: https://fuchsia.googlesource.com/build/+/master/dart/dart_tool.gni "dart_tool target"
[target-flutter]: https://github.com/flutter/engine/blob/master/build/flutter_app.gni "flutter_app target"
[target-test]: https://fuchsia.googlesource.com/build/+/master/dart/dart_test.gni "dart_test target"
[flutter]: https://flutter.io/ "Flutter"
[fidl]: https://fuchsia.googlesource.com/fidl/+/master/fidl.gni "FIDL"
[logger-doc]: https://www.dartdocs.org/documentation/logging/0.11.3%2B1/logging/Logger-class.html
