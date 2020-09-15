# FIDL Tests & GN

This document standardizes the way we define and organize FIDL tests in the GN
build system, respecting the following goals:

*   **Name consistently**. If Rust uses `fx test fidl_rust_conformance_tests`,
    then Go should use `fx test fidl_go_conformance_tests`. There should be no
    need to continually look these up in the contributing doc because they're
    all different.
*   **Run what you need**. If there are multiple test executables in a given
    category, it should be easy to only run the one you care about using `fx
    test`.
*   **Run on host**. Wherever possible, we should provide ways to run tests on
    host, and in a predictable way. The quick edit-build-run cycle on host can
    greatly improve productivity.
*   **Follow best practices**. Our approach should be aligned with Fuchsia best
    practices, e.g. following the fuchsia-pkg URL specification, working well
    with `fx test`, etc.

## Terminology

This document uses the following terminology:

*   **target**: a [GN target][gn-targets] defined in a BUILD.gn file
*   **toolchain**: see `fx gn help toolchain`
*   **host**: a developer's platform, specifically linux or mac
*   **device**: a Fuchsia platform, either physical or emulated (i.e. qemu)
*   **package**: a [Fuchsia package][package]; the unit of distribution in
    Fuchsia
*   **component**: a [Fuchsia component][components]; the unit of executable
    software in Fuchsia

## Naming {#naming}

General guidelines:

*   Use hyphens in [package names][package_names], and underscores in everything else.
*   End names with the plural `_tests` rather than the singular `_test`.
*   Use full, descriptive, unique names for packages, components, and binaries.

The last point means preferring full names like `fidl_rust_conformance_tests`
over contextual names like `conformance_tests`. It might seem verbose and
redundant to repeat "fidl" and "rust" at the directory, package, component, and
binary level. But the fact is that these names must be unique, and it is better
to make them unique in a consistent way rather than remembering odd rules like
`fidl-bindings-test` is for Dart and `fidl-test` is for C.

Names should use the following scheme, joining parts with hyphens or
underscores:

> _tool_ [ _bindings_ ] [ _category_ [ _subcategory_ ] ] **tests**

Where _tool_ is one of:

*   **fidl**: FIDL runtime support
*   **fidlc**: FIDL compiler frontend
*   **fidlgen**: FIDL compiler backend
*   **gidl**, **measure_tape**, etc.: other tools

And the other parts are:

*   _bindings_
    *   One of **c**, **llcpp**, **hlcpp**, **rust**, **go**, **dart** (note:
        **hlcpp**, _not_ **cpp**)
*   _category_, _subcategory_
    *   Example categories: **conformance**, **types**, **parser**, **lib**
    *   Do _not_ use: **frontend**, **backend**, **bindings** (_tool_
        distinguishes these)

## Hierarchy

Every BUILD.gn file that defines tests should include a `"tests"` group:

```gn
group("tests") {
  testonly = true
  deps = [ ... ]  # not public_deps
}
```

These are aggregated in "tests" groups of BUILD.gn files in parent directories.
The root "tests" group (for some portion of the codebase, e.g.
src/lib/fidl/BUILD.gn) should be included in bundles/fidl/BUILD.gn. This enables
`fx set ... --with //bundles/fidl:tests` to include all FIDL tests in the build.
(The tests are also run in CQ because `//bundles/buildbot:core` includes
`//bundles/fidl:tests`.)

## Device tests

Assume we have a `:fidl_foo_tests_bin` target that produces a `fidl_foo_tests`
binary. To wrap this in a package, start with a `unittest_package`:

```gn
import("//build/test/test_package.gni")
import("//build/testing/environments.gni")

unittest_package("fidl-foo-tests") {
  deps = [ ":fidl_foo_tests_bin" ]
  tests = [
    {
      name = "fidl_foo_tests"
      environments = basic_envs
    },
  ]
}
```

We can now run the test by package name (`fx test fidl-foo-tests`) or by
component name (`fx test fidl_foo_tests`). For single-test packages like this,
**use the component name in documentation** (e.g. contributing_to_fidl.md,
`"Test:"` lines in commit messages).

For multiple device tests, collect them all in a **single package** instead of
making separate packages. For example, suppose we split `fidl_foo_tests` into
`fidl_foo_unit_tests` and `fidl_foo_integration_tests`:

```gn
import("//build/test/test_package.gni")
import("//build/testing/environments.gni")

unittest_package("fidl-foo-tests") {
  deps = [
    ":fidl_foo_unit_tests_bin",
    ":fidl_foo_integration_tests_bin",
  ]
  tests = [
    {
      name = "fidl_foo_unit_tests"
      environments = basic_envs
    },
    {
      name = "fidl_foo_integration_tests"
      environments = basic_envs
    },
  ]
}
```

Most of the time `unittest_package` is enough. If your test needs any component
features, services, etc., you must instead use `test_package` and write a
component manifest file:

```gn
# BUILD.gn
import("//build/test/test_package.gni")
import("//build/testing/environments.gni")

test_package("fidl-foo-tests") {
  deps = [ ":fidl_foo_tests_bin" ]
  tests = [
    {
      name = "fidl_foo_tests"
      environments = basic_envs
    },
  ]
}

# meta/fidl_foo_tests.cmx
{
    "program": {
        "binary": "test/fidl_foo_tests"
    },
    "sandbox": {
        "services": [
            "fuchsia.logger.LogSink",   # some example services
            "fuchsia.process.Launcher"
        ]
    }
}
```

The manifest path defaults to meta/fidl\_too\_tests.cmx in this case. As with
`unittest_package`, you can include multiple test components. Each one will need
its own component manifest file.

## Host tests

Assume we have a `:fidl_bar_tests` target that produces a `fidl_bar_tests` host
test binary, defined using one of the test templates: `test`, `go_test`, etc. We
must **ensure that GN is in `$host_toolchain` when it reaches that target**,
otherwise it will try to build it for Fuchsia:

```gn
groups("tests") {
  testonly = true
  deps = [ ":fidl_bar_tests($host_toolchain)" ]
}
```

(Always put `($host_toolchain)` in the BUILD.gn file's `tests` group, not in
//bundles/fidl:tests.)

This will create a test\_spec entry named `host_x64/fidl_bar_tests`, which will
end up in out/default/tests.json:

```json
{
  "command": [ "host_x64/fidl_bar_tests", "--test.timeout", "5m" ],
  "cpu": "x64",
  "label": "//PATH/TO/BAR:fidl_bar_tests(//build/toolchain:host_x64)",
  "name": "host_x64/fidl_bar_tests",
  "os": "linux",
  "path": "host_x64/fidl_bar_tests",
  "runtime_deps": "host_x64/gen/PATH/TO/BAR/fidl_bar_tests.deps.json"
}
```

Running `fx test fidl_bar_tests` works because of the "name" field in
tests.json.

## Host/Device tests

Tests that run both on host and device fall in two categories. In the first
category, the test target simply builds under either toolchain. For example:

```gn
import("//build/test/test_package.gni")
import("//build/testing/environments.gni")

rustc_test("fidl_rust_conformance_tests") {  # host test name
  ...
}

unittest_package("fidl-rust-tests") {
  deps = [ ":fidl_rust_conformance_tests" ]
  tests = [
    {
      name = "fidl_rust_conformance_tests"   # device test name
      environments = basic_envs
    },
  ]
}

group("tests") {
  testonly = true
  deps = [
    ":fidl_rust_conformance_tests($host_toolchain)",
    ":fidl-rust-tests",
  ]
}
```

We can now run the test both ways:

*   on device: `fx test fidl_rust_conformance_tests --device`
*   on host: `fx test fidl_rust_conformance_tests --host`

In the second category, the device and host tests share source code, but they
are sufficiently different that they must be defined by separate targets. This
time, **use the same name for the _host_ test target and the component name**.
This will require choosing a different name for the _device_ test target. For
example:

```gn
import("//build/test/test_package.gni")
import("//build/testing/environments.gni")

source_set("conformance_test_sources") {
  ...
}

test("fidl_hlcpp_conformance_tests") {       # host test name
  ...
  deps = [
    ":conformance_test_sources",
    ...
  ]
}

test("fidl_hlcpp_conformance_tests_fuchsia") {
  # We had to append _fuchsia to distinguish this from the host test target.
  # But we want the binary name to be fidl_hlcpp_conformance_tests to match
  # up with the same-named test component in the unittest_package.
  output_name = "fidl_hlcpp_conformance_tests"
  ...
  deps = [
    ":conformance_test_sources",
    ...
  ]
}

unittest_package("fidl-hlcpp-tests") {
  deps = [ ":fidl_hlcpp_conformance_tests_fuchsia" ]
  tests = [
    {
      name = "fidl_hlcpp_conformance_tests"  # device test name
      environments = basic_envs
    },
  ]
}

group("tests") {
  testonly = true
  deps = [
    ":fidl_hlcpp_conformance_tests($host_toolchain)",
    ":fidl-hlcpp-tests"
  ]
}
```

Now, we can run the test both ways:

*   on device: `fx test fidl_hlcpp_conformance_tests --device`
*   on host: `fx test fidl_hlcpp_conformance_tests --host`

## C++ Zircon tests

C++ tests in Zircon are usually defined using `zx_test`. This template behaves
differently from most others like `test`, `rustc_test`, `go_test`, etc.: it
appends `-test` to the binary name by default. **Do not rely on this**, for two
reasons:

1.  The [naming guidelines](#naming) require a `_tests` suffix, not `-test`.
2.  This behaviour might change during build unification.

Instead, specify the target and binary name explicitly:

```gn
zx_test("fidlc_unit_tests") {
  output_name = "fidlc_unit_tests"  # explicit binary name
  ...
}
```

## Rust unit tests

Rust libraries are often defined like this:

```gn
rustc_library("baz") {
  with_unit_tests = true
  ...
}
```

This automatically creates a `baz_test` target that builds a `baz_lib_test`
binary. **Do not use this**, for two reasons:

1.  The [naming guidelines](#naming) require a `_tests` suffix, not `-test`.
2.  It will be
    [deprecated](https://fuchsia.googlesource.com/fuchsia/+/9d9f092f2b30598c3929bd30d0058d4e052bb0f4/build/rust/rustc_library.gni#91)
    soon.

Instead of `with_unit_tests`, write a separate `rustc_test` target with an
appropriate name:

```gn
rustc_library("baz") {
  ...
}

rustc_test("fidl_baz_tests") {
  ...
}
```

## Grouping

Suppose we have the following test structure:

*   FIDL Rust
    *   Device
        *   Conformance
        *   Integration
    *   Host
        *   Conformance

We should have test targets for the leaves:

*   `fx test fidl_rust_conformance_tests`
*   `fx test fidl_rust_integration_tests`

We should **not** make additional targets for running various subsets of the
tests. Using `fx test`, we can already

*   run all tests: `fx test //path/to/fidl/rust`
*   run all device tests: `fx test //path/to/fidl/rust --device`
*   run all host tests: `fx test //path/to/fidl/rust --host`

## References

*   [Source code layout][source_code_layout]
*   [Building components][building_components]
*   [Run tests as components][run_tests_as_components]
*   [Fuchsia component manifest][component_manifest]
*   [Fuchsia package URLs][package_url]

<!-- xrefs -->
[gn-targets]: https://gn.googlesource.com/gn/+/refs/heads/master/docs/language.md#Targets
[package]: /docs/concepts/packages/package.md
[components]: /docs/concepts/components/v2
[run_tests_as_components]: /docs/development/testing/run_tests_as_components.md
[component_manifest]: /docs/concepts/components/v1/component_manifests.md
[package_url]: /docs/concepts/packages/package_url.md
[package_names]: /docs/concepts/packages/package_url.md#package_identity
[source_code_layout]: /docs/concepts/source_code/layout.md
[building_components]: /docs/development/components/build.md
