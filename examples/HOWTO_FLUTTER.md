# Flutter Module Development

This directory demonstrates how you create modules with Dart and Flutter. At the
moment this document assumes that every module gets built as part of the core
fuchsia build and included in the bootfs.

# Example Modules

## Hello World

(located in `hello_world_flutter/`)

This example demonstrates how to create a minimal flutter module and implement
the `Module` interface. It shows a simple flutter text widget displaying
"Hello, world!" on the screen.

## Counter

(located in `counter_flutter/`)

This example consists of two modules: parent and child. The parent module runs
as the top-level module and spawns a child module. Each module displays the
shared counter value, and the value can be increased or decreased from both
modules. The main purpose of this example is to demonstrate the following
aspects:

* Starting a new module as a child, using the `Story` service.
* Composing the child's UI into the parent's widget tree using `ChildView`.
* Exchanging data between modules using a shared `fuchsia::modular::Link` service.

## Running the Examples on Fuchsia

You can run an example module without going through the full-blown user shell.
The available URLs for for flutter module examples are:

* `example_flutter_hello_world`
* `example_flutter_counter_parent`

After a successful build of fuchsia, type the following command from the zx
console to run the device runner with the dev user shell:

```
device_runner --user_shell=dev_user_shell --user_shell_args=--root_module=example_flutter_hello_world
```

# Basics

A flutter module is a flutter app which provides a `Module` service
implementation. Roughly, the main function of a flutter module should look like
the following.

```dart
final StartupContext _context = new StartupContext.fromStartupInfo();

// Keep this module reference as a global variable to keep it for the lifetime
// of this module.
ModuleImpl _module;

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
      _module = new ModuleImpl()..bind(request);
    },
    Module.serviceName,
  );

  runApp(/* A top level flutter widget goes here, which will be the root Mozart
            view for this module. */);
}
```

When the `Module` implementation wants to update some values in `State` of a
`StatefulWidget`, it can obtain the `State` object via `GlobalKey`s or other
means. (See the "counter" example code.)

# Importing Packages

## Adding Dependency to BUILD.gn

To import a dart package written within the fuchsia tree, the dependency should
be added to the project's `BUILD.gn`. The `BUILD.gn` file for the hello_world
example this:

```
import("//third_party/flutter/build/flutter_app.gni")

flutter_app("example_flutter_hello_world") {
  main_dart = "lib/main.dart"
  deps = [
    "//garnet/public/lib/ui/views/fidl:fidl_dart",         # view fidl dart bindings
    "//peridot/public/lib/module/fidl:fidl_dart",          # module fidl dart bindings
    "//peridot/public/lib/story/fidl:fidl_dart",           # story fidl dart bindings
    "//third_party/dart-pkg/git/flutter/packages/flutter", # flutter package
    "//topaz/public/lib/app/dart",                         # needed for StartupContext
    "//topaz/public/lib/fidl/dart",                        # fidl dart libraries
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
For example, let's say we want to import and use the `module.fidl` file (located
in `<fuchsia_root>/peridot/services/module`) in our dart code. We should
first look at the `BUILD.gn` file in the same directory. In this file we can see
that the `module.fidl` file is included in the `fidl("module")` target.

```
# For consumption outside modular.
fidl("module") {
  sources = [
    "link.fidl",
    "module.fidl",   # This is the fidl we want to use for now.
    "module_controller.fidl",
    "module_context.fidl",
  ]
}
```

This means that we need to depend on this group of fidl files named 'story'. In
our module's `BUILD.gn`, we can add the dependency with the following syntax:

```
"//<dir>:<fidl_target_name>_dart"
```

Let's look at the `BUILD.gn` in our hello_world example module directory again.
It has `"//peridot/public/lib/story/fidl:fidl_dart",` as a dependency. (Note the
`_dart` added at the end.)

Once this is done, we can use all the interfaces defined in `.fidl` files
contained in this `story` fidl target from our code.

## Importing in Dart Code

Once the desired package is added as a BUILD.gn dependency, the dart files in
those packages can be imported in our dart code. Importing dart packages in
fuchsia looks a bit different than normal dart packages. Let's look at the
import statements in `main.dart` of the hello_world example.

```dart
import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.module/module.fidl.dart';
import 'package:apps.modular.services.module/module_context.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/widgets.dart';
```

To import things in the fuchsia tree, we use dots (`.`) instead of slashes (`/`)
as path delimiter. For FIDL-generated dart files, we add `.dart` at the end of
the corresponding fidl file path. (e.g. `module.fidl.dart`)


# Using FIDL Dart Bindings

The best place to start is the dartdoc comments in this file:
https://fuchsia.googlesource.com/topaz/public/+/master/lib/fidl/dart/lib/src/bindings/interface.dart

Also, refer to the example flutter module code to see how the
`InterfaceHandle<Foo>`, `InterfaceRequest<Foo>`, `InterfacePair<Foo>`,
`FooProxy` classes are used.

## Things to Watch Out For

### Handles Can Only Be Used Once

Once an `InterfaceHandle<Foo>` is bound to a proxy, the handle cannot be used in
other places. Often, in case you have to share the same service with multiple
parties (e.g. sharing the same `fuchsia::modular::Link` service across multiple modules), the
service will provide a way to obtain a duplicate handle (e.g. `fuchsia::modular::Link::Dup()`).

You can also call `unbind()` method on `ProxyController` to get the usable
`InterfaceHandle<Foo>` back, which then can be used by someone else.

### Proxies and Bindings Should Be Closed Properly

You need to explicitly close `FooProxy` and `FooBinding` objects that are bound
to channels, when they are no longer in use. You do not need to explicitly close
`InterfaceRequest<Foo>` or `InterfaceHandle<Foo>` objects, as those objects
represent unbound channels.

If you don't close or unbind these objects and they get picked up by the garbage
collector, then FIDL will terminate the process and (in debug builds) log the
Dart stack for when the object was bound. The only exception to this rule is for
*static* objects that live as long as the isolate itself. The system is able to
close these objects automatically for you as part of an orderly shutdown of the
isolate.

If you are writing a Flutter widget, you can override the `dispose()` function
on `State` to get notified when you're no longer part of the tree. That's a
common time to close the proxies used by that object as they are often no longer
needed.

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
sky_engine:file:///<abs_fuchsia_root>/third_party/dart-pkg/git/flutter/bin/cache/pkg/sky_engine/lib/
```

You might have to relaunch Atom to get everything working correctly. With this
`.packages` files, you get all dartanalyzer errors/warnings, jump to definition,
auto completion features.
