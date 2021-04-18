# GN templates for building and testing components.

GN templates for defining Fuchsia components and packages for production and
testing.

See: [https://fuchsia.dev/fuchsia-src/development/components/build]

## Using

Add this line to your `BUILD.gn` file:
```
import("//build/components.gni")
```

## Examples

See the tests under `tests/` for usage examples.
See individual `.gni` files for more details.
