# Testing Rust code

This document describes best practices for writing tests for Rust code, and
paired with ["Running tests as components"][component_tests] describes how to
component-ize, package, and run these tests.

This document is targeted towards developers working inside of `fuchsia.git`,
and the workflow described is unlikely to work for SDK consumers.

The source code for this tutorial is available at
[`//examples/hello_world/rust`][example-src].

## Unit tests

### Adding tests to code

The idiomatic way for adding Rust unit tests works just as well inside of
Fuchsia as it does outside, and can be easily accomplished by dropping the
following snippet into the bottom of whatever test you want to write:

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/hello_world/rust/src/main.rs" region_tag="test_mod" adjust_indentation="auto" %}
```

This will cause a new mod named `tests` to be created, and this mod will only be
included when building unit tests. Any functions annotated with `#[test]` will
be run as a test, and if the function successfully returns then the test passes.

For tests exercising asynchronous code, use the
`#[fasync::run_until_stalled(test)]` annotation as an alternative to
using an asynchronous executor.

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/hello_world/rust/src/main.rs" region_tag="async_test" adjust_indentation="auto" %}
```

### Building tests

These tests can be automatically built by the `rustc_binary` GN template by
setting `with_unit_tests = true`. This would typically go in a `BUILD.gn` file
next to the `src` directory containing the rust code.

```gn
import("//build/rust/rustc_binary.gni")
```

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/hello_world/rust/BUILD.gn" region_tag="rustc_tests" adjust_indentation="auto" %}
```

Setting `with_unit_tests = true` causes this build rule to generate two
different executables, one with the provided and one with `_bin_test` appended
to the provided name. In our example here, that means that one executable named
`hello_world_rust` will be created and one executable named
`hello_world_rust_bin_test` will be created.

### Packaging and running tests

For the Hello world example, the test package needs to reference the generated
targets, `bin_test` and `hello_world_rust_bin_test`:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="examples/hello_world/rust/BUILD.gn" indented_block="^test_package\(\"hello_world_rust_tests\"\) {" %}
```

To run the tests run:

```sh
fx run-test hello_world_rust_tests
```

Note: that in order to use `fx run-test`, you can't override
`package_name="..."` in your `package`  or `test_package` declaration. This
issue is tracked by BLD-338.


For information on packaging and running tests, please refer to the
[documentation on running tests as components][component_tests].


[component_tests]:/docs/development/testing/running_tests_as_components.md
[example-src]: /examples/hello_world/rust
