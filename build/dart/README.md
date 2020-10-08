# GN templates for building and testing Dart components.

GN templates for defining Dart components

See: [https://fuchsia.dev/fuchsia-src/development/components/build]

## Using

Add this line to your `BUILD.gn` file:
```
import("//build/dart/dart_component.gni")
```

## Examples

See the tests under `tests/` for usage examples.
See individual `.gni` files for more details.

## Compilation Modes

Dart components can be compiled in various different compilation modes. The component can either be compiled in JIT mode or AOT mode and
each of these can optionally be compiled in 'product' mode which runs
in a stripped down VM. By default, the dart_component will pick the
compilation mode based on the following:

- non-product JIT if debug
- non-product AOT if release
- product AOT if dart_force_product == true

The compilation mode can be set explicity as a gn arg by setting the
`dart_default_build_cfg` variable.

Product mode can be set by setting the `dart_force_product` variable to true.

## Template Structure

Flutter components and Dart components share much of the same functionality and only differ in their runners and some dependencies. The dart_component will delegate much of the work to the flutter_dart_component target which resides at //build/flutter/internal/flutter_dart_component.gni. The dart_component just sets up the runner dependencies before forwarding to the flutter_dart_component target.
