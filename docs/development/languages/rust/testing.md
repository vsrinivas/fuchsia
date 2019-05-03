# Testing Rust code

This document describes best practices for writing tests for Rust code, and
paired with ["Running tests as components"][component-tests] describes how to
component-ize, package, and run these tests.

This document is targeted towards developers working inside of `fuchsia.git`,
and the workflow described is unlikely to work for SDK consumers.

An example setup of a test component in Rust is available at
`//examples/hello_world/rust`.

## Unit tests

### Adding tests to code

The idiomatic way for adding Rust unit tests works just as well inside of
Fuchsia as it does outside, and can be easily accomplished by dropping the
following snippet into the bottom of whatever test you want to write:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        assert_eq!(true, true);
    }
}
```

This will cause a new mod named `tests` to be created, and this mod will only be
included when building unit tests. Any functions annotated with `#[test]` will
be run as a test, and if the function successfully returns then the test passes.

If a test is going to be exercising asynchronous code, the test could instead be
annotated with `#[fasync::run_until_stalled(test)]` to prevent needing to
manually create and use an asynchronous executor.

```rust
use fuchsia_async as fasync;

#[fasync::run_until_stalled(test)]
async fn my_test() {
    let some_future = async { 4 };
    assert_eq!(await!(some_future), 4);
}

```

### Building tests

These tests can be automatically built by the `rustc_binary` GN template by
setting `with_unit_tests = true`. This would typically go in a `BUILD.gn` file
next to the `src` directory containing the rust code.

```GN
import("//build/rust/rustc_binary.gni")

rustc_binary("bin") {
  # The `name` field is optional, and will default to the target name
  name = "hello_world_rust"

  with_unit_tests = true
  edition = "2018"

  deps = [
    "//garnet/public/rust/fuchsia-async",
  ]
}
```

Setting `with_unit_tests = true` causes this build rule to generate two
different executables, one with the provided and one with `_bin_test` appended
to the provided name. In our example here, that means that one executable named
`hello_world_rust` will be created and one executable named
`hello_world_rust_bin_test` will be created.

### Packaging and running tests

For information on packaging and running tests, please refer to the
[documentation on running tests as components][component_tests].

[component_tests]:../../tests/running_tests_as_components.md
