# Flutter Module Development

This directory demonstrates how you modules with Dart and Flutter. At the moment
this document assumes that every module gets built as part of the core fuchsia
build and included in the bootfs.

# Examples Modules

## Hello World Example

(located in `hello_world/`)

This example demonstrates how to create a minimal flutter module and implement
the `Module` interface. It shows a simple flutter text widget displaying
"Hello, world!" on the screen.

## Counter Example

(located in `counter/`)

This example consists of two modules: parent and child. The parent module runs
as the top-level module and spawns a child module. Each module displays the
shared counter value, and the value can be increased or decreased from both
modules. The main purpose of this example is to demonstrate the followings:

* Starting a new module as a child, using the Story service.
* Composing the child's UI into the parent's widget tree using `ChildView`.
* Exchanging data between modules using a shared Link service.

## Running the Examples on Fuchsia

To run these modules directly without a user shell, you first need to edit the
dummy user shell code (located at `src/user_runner/dummy_user_shell.cc`) so that
it points to the module you want to run. Change the following line:

    constexpr char kExampleRecipeUrl[] = "file:///system/apps/example_recipe";

and change the url to the desired module url. The available URLs for flutter
module examples are:

* `file:///system/apps/example_flutter_hello_world`
* `file:///system/apps/example_flutter_counter_parent`

After changing this line, build fuchsia, and type the following command from the
mx console to run the device runner with the dummy user shell:

    @ bootstrap device_runner --user-shell=dummy_user_shell

# Basics

A flutter module is a flutter app which also provides a `Module` service
implementation. Roughly, the main function of a flutter module should look like
the following.

```dart
ApplicationContext _context = new ApplicationContext.fromStartupInfo();

class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  // Implement all the methods defined in the Module interface below...

  @override
  void initialize(...) { ... }

  @override
  void stop(void callback()) { ... }
}

void main() {
  _context.outgoingServices.addServiceForName(
    (InterfaceRequest<Module> request) {
      new ModuleImpl().bind(request);
    },
    Module.serviceName,
  );

  runApp(/* A top level flutter widget goes here, which will be the root Mozart
            view for this module. */);
}
```

When the `Module` implementation wants to update some values in `State` of a
`StatefulWidget`, it can obtain the `State` object via `GlobalKey`s or other
means. (See the counter example code)

# Importing Packages

## Adding Dependency to BUILD.gn

To import a dart package written within the fuchsia tree, the dependency should
be added to the project’s `BUILD.gn`. The `BUILD.gn` file for the hello_world
example this:

```
import("//flutter/build/flutter_app.gni")

flutter_app("example_flutter_hello_world") {
  main_dart = "lib/main.dart"
  deps = [
    "//apps/modular/lib/app/dart",              # needed for ApplicationContext
    "//apps/modular/services/story:story_dart", # story fidl dart bindings
    "//apps/mozart/services/views:views_dart",  # view fidl dart bindings
    "//lib/fidl/dart",                          # fidl dart libraries
    "//lib/flutter/packages/flutter",           # flutter package
  ]
}
```

There are two types of dart packages we can include as `BUILD.gn` dependencies.

### 1. Normal Dart Packages

Any third-party dart packages, or regular dart packages manually written in the
fuchsia tree. Import them with their relative paths from the `<fuchsia_root>`
directory followed by two slashes. Third-party dart packages are usually located
at `//third_party/dart-pkg/pub/<package_name>`.

### 2. FIDL-Generated Dart Bindings

To use any FIDL generated dart bindings, you need to first look at the
`BUILD.gn` defining the `fidl` target that contains the desired `.fidl` file.
For example, let’s say we want to import and use the `module.fidl` file (located
in `<fuchsia_root>/apps/modular/services/story`) in our dart code. We should
first look at the `BUILD.gn` file in the same directory. In this file we can see
that the `module.fidl` file is included in the `fidl(“story”)` target.

```
# For consumption outside modular.
fidl("story") {
  sources = [
    "link.fidl",
    "module.fidl",   # This is the fidl we want to use for now.
    "module_controller.fidl",
    "story.fidl",
  ]

  deps = [
    "//apps/modular/services/document_store",
  ]
}
```

This means that we need to depend on this group of fidl files named “story”. In
our module’s `BUILD.gn`, we can add the dependency with the following syntax:

```
"//<dir>:<fidl_target_name>_dart"
```

Let's look at the `BUILD.gn` in our hello_world example module directory again.
It has `"//apps/modular/services/story:story_dart",` as a dependency. (Note the
`_dart` added at the end.)

Once this is done, we can use all the interfaces defined in `.fidl` files
contained in this `story` fidl target from our code.

## Importing in Dart Code

Once the desired package is added as a BUILD.gn dependency, the dart files in
those packages can be imported in our dart code. Importing dart packages in
fuchsia looks a bit different than normal dart packages. Let’s look at the
import statements in `main.dart` of the hello_world example.

```dart
import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/widgets.dart';
```

To import things in the fuchsia tree, we use dots(`.`) instead of slashes(`/`)
as path delimiter. For FIDL-generated dart files, we add `.dart` at the end of
the corresponding fidl file path. (e.g. `module.fidl.dart`)


# Using FIDL Dart Bindings

The best place to start is the dartdoc comments in this file:
https://fuchsia.googlesource.com/fidl/+/master/dart/src/bindings/interface.dart

Also, refer to the example flutter module code to see how the
`InterfaceHandle<Foo>`, `InterfaceRequest<Foo>`, `FooProxy` classes are used.


# Other Useful Tips

## Getting the Atom dartlang plugin to work correctly

You need to have the correct `.packages` file generated for the dart packages in
fuchsia tree. After building fuchsia, run this script form the terminal of your
development machine:

```
<fuchsia_root>$ scripts/symlink-dot-packages.py
```

Also, for flutter projects, the following line should be manually added to the
`.packages` file manually (fill in the fuchsia root dir of yours):

```
sky_engine:file:///<abs_fuchsia_root>/lib/flutter/bin/cache/pkg/sky_engine/lib/
```

You might have to relaunch Atom to get everything working correctly. With this
`.packages` files, you get all dartanalyzer errors/warnings, jump to definition,
auto completion features.
