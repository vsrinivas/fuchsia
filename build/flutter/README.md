# GN templates for building and testing Flutter components.

GN templates for defining Flutter components

See: [https://fuchsia.dev/fuchsia-src/development/components/build]

## Using

Add this line to your `BUILD.gn` file:
```
import("//build/fluter/flutter_component.gni")
```

## Examples

See the tests under `tests/` for usage examples.
See individual `.gni` files for more details.

## Compilation Modes

Flutter components can be compiled in various different compilation modes. The component can either be compiled in JIT mode or AOT mode and
each of these can optionally be compiled in 'product' mode which runs
in a stripped down VM. By default, the flutter_component will pick the
compilation mode based on the following:

- non-product JIT if debug
- non-product AOT if release
- product AOT if flutter_force_product == true

The compilation mode can be set explicity as a gn arg by setting the
`flutter_default_build_cfg` variable.

Product mode can be set by setting the `flutter_force_product` variable to true.

## Flutter Assets

Flutter applications can define assets in their pubspec.yaml file [https://flutter.dev/docs/development/ui/assets-and-images]. The flutter_component target will look at the pubspec file during compilation and generate an AssetManifest.json file and ensure that all assets get included in the resulting package.

## Template Structure

Flutter components and Dart components share much of the same functionality and only differ in their runners and some dependencies. The flutter_component will delegate much of the work to the flutter_dart_component target which resides at //build/flutter/internal/flutter_dart_component.gni. Much of the work that the flutter_component does is just setting up the flutter driver dependencies and other runner dependencies before forwarding to the flutter_dart_component target.
