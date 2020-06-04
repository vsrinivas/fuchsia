# Building components {#building-components}

This document demonstrates how to build and test a component, highlighting best
practices for defining packages, components, and their tests.

## Concepts {#concepts}

You should understand the following concepts before building a component:

**[Packages][glossary-package]** are the unit of software distribution on
Fuchsia. Packages are a collection of files with associated paths that are
relative to the base of the package. For instance, a package might contain an
ELF binary under the path `bin/hello_world`, and a JSON file under the path
`data/config.json`. Grouping files into a package is required in order to push
these files to the device.

**[Components][glossary-component]** are the unit of software execution on
Fuchsia. All software on Fuchsia except for the kernel image and usermode
bootstrap program is defined as a component.

A component is defined by a
**[component manifest][glossary-component-manifest]**. Components typically
include additional files, such as executables and data assets that they need at
runtime.

Developers must define their software in terms of packages and components,
whether for building production software or for writing their tests.

**[Component instances][glossary-component-instance]** see at runtime the
contents of their package as read-only files under the path `/pkg`. Defining two
or more components in the same package doesn't grant each component access to
the other's capabilities. However it can guarantee to one component that the
other is available. Therefore if a component attempts to launch an instance of
another component, such as in an integration test, it can be beneficial to
package both components together.

Components are instantiated in a few ways, all of which somehow specify their
URL. Typically components are launched by specifying their package names and
path to their component manifest in the package, using the
**[`fuchsia-pkg://` scheme][glossary-fuchsia-pkg-url]**.

## GN templates

[GN][glossary-gn] is the meta-build system used by Fuchsia. Fuchsia extends GN
by defining templates. Templates provide a way to add to GN's built-in target
types. Below we will review various GN templates that can be used to define
packages, components, and their tests.

### Defining components, packages, and tests using GN templates {#defining}

Below is a hypothetical package containing one component that runs a C++
program. The example uses the following templates:

*   [`fuchsia_component.gni`](/src/sys/build/fuchsia_component.gni)
*   [`fuchsia_package.gni`](/src/sys/build/fuchsia_package.gni)

```gn
import("//src/sys/build/components.gni")

executable("my_program") {
  sources = [ "my_program.cc" ]
}

fuchsia_component("my-component") {
  manifest = "meta/my_program.cmx"
  resources = [
    {
      source = "$root_out_dir/my_program"
      destination = "bin/my_program"
    }
  ]
  deps = [ ":my_program" ]
}

fuchsia_package("my-package") {
  components = [ ":my-component" ]
}
```

The file `my_program.cmx` should include at least the following:

```json
{
    "program": {
        "binary": "bin/my_program"
    }
}
```

Note the following details:

*   This example imports `"//src/sys/build/components.gni"`. This single import
    includes all of
    [`fuchsia_component.gni`](/src/sys/build/fuchsia_component.gni),
    [`fuchsia_package.gni`](/src/sys/build/fuchsia_package.gni), and
    [`fuchsia_test.gni`](/src/sys/build/fuchsia_test.gni).
*   This example defines an `executable()`, which is used to build a C++
    program. This is for illustrative purposes - a component can launch all
    sorts of programs.
*   This example defines a `fuchsia_component()` which depends on the
    `executable()`. The component definition attaches a manifest, which
    references the executable to be launched under the given path
    `bin/my_program`. We also define a resource, the executable built above to
    be included in the said path. \
    The source path `"$root_out_dir/my_program"` is where
    `executable("my_program")` will deposit its output, which is by convention
    in Fuchsia's build. Note that it's possible to specify
    `executable.output_name` - this overrides the default output name for
    `executable()`, which is derived from its target name. In this case the
    `source` parameter needs to change accordingly. \
    It is possible of course to include multiple resource files in a component
    target. Any and all resources defined will be included in any package that
    depends on them. It is also possible to have multiple packages depend on the
    same component target. \
    The manifest must be either a `.cmx` file in [cmx format][cmx-format] or a
    `.cml` file in [cml format][cml-format]. \
    Finally, the destination path for the manifest is not specified, but rather
    inferred from the component's name. In this example, the manifest path will
    be `meta/my-component.cmx`.
*   This example defines a `fuchsia_package()` which depends on the component.
    Note that packages can depend on any number of components. It is also
    possible, though rare, to define packages with no components in them, just
    data files. These packages will have `deps` on targets that provide these
    definitions, of the template defined in
    [`fuchsia_resources.gni`](/src/sys/build/fuchsia_resources.gni). \
    [By convention][package-name] package names contain dashes (`-`) but not
    underscores (`_`), and the component name above follows suit. \
    Both the component and package names are derived from their target names.
    They both take an optional `component_name` and `package_name` parameter
    respectively as an override. \
    In the example above, these names come together to form the URL for
    launching the component:
    `fuchsia-pkg://fuchsia.com/my-package#meta/my-component.cmx`.

### Test packages {#test-packages}

Test packages are packages that contain at least one component that will be
launched as a test. Test packages are defined using
[`fuchsia_test_package.gni`](/src/sys/build/fuchsia_test_package.gni). This
template can be used to define all sorts of tests, although it's most useful for
integration tests -- tests where other components in addition to the test itself
participate in the test. See [below](#unit-tests) for templates that specialize
in unit testing.

```gn
import("//src/sys/build/components.gni")

executable("my_test") {
  sources = [ "my_test.cc" ]
  testonly = true
}

fuchsia_component("my-test-component") {
  testonly = true
  manifest = "meta/my_test.cmx"
  resources = [
    {
      source = "$root_out_dir/my_test"
      destination = "bin/my_test"
    }
  ]
  deps = [ ":my_test" ]
}

executable("my_program_under_test") {
  sources = [ "my_program_under_test.cc" ]
}

fuchsia_component("my-other-component-under-test") {
  manifest = "meta/my_component_under_test.cmx"
  resources = [
    {
      source = "$root_out_dir/my_program_under_test"
      destination = "bin/my_program_under_test"
    }
  ]
  deps = [ ":my_program_under_test" ]
}

fuchsia_test_package("my-integration-test") {
  test_components = [ ":my-test-component" ]
  components = [ ":my-other-component-under-test" ]
  environments = [ vim2_env ]
}

group("tests") {
  deps = [ ":my-integration-test" ]
  testonly = true
}
```

Note the following details:

*   This example defines `"my-test-component"` which is assumed to implement a
    test. Commonly this is done using some testing framework such as C++
    Googletest, Rust Cargo test, etc'.
*   To launch this test, you can use [`fx test`][fx-test].
*   The test is packaged with another component,
    `"my-other-component-under-test"`. Presumably this component participates in
    the test. For instance, the component under test might implement a protocol,
    and the test launches it and connects to it as a client while asserting
    correct behavior from the client's perspective. \
    Packaging the component under test together with the test component
    guarantees that the component under test will be available for launch while
    the test is running, and will be built at the same version as the test. If
    this weren't the case, and instead the test assumed that the component under
    test was present in another package that's already installed on the target
    device, then the test would be exposed to side effects and version skew.
    Packaging the test with its dependencies makes it more hermetic.
*   Note the `environments` parameter. `fuchsia_test_package()` can optionally
    take [`test_spec.gni`](/build/testing/test_spec.gni) parameters to override
    the default testing behavior. In this example, this test is configured to
    run on VIM2 devices.
*   Finally, this example defines a `group()` to contain all the tests (which we
    have exactly one of). This is a [recommended practice][source-code-layout]
    for organizing targets across the source tree.

### Language-specific component templates {#language-specific-component-templates}

To simplify defining components in commonly used languages, simplified templates
are provided.

   * {C++}

   ```gn
   import("//src/sys/build/components.gni")

   fuchsia_cpp_component("my-component") {
     sources = [ "main.cc" ]
     deps = [ ... ]
   }
   ```

   See also: [`fuchsia_cpp_component.gni`](/src/sys/build/fuchsia_cpp_component.gni)

   Additional [`executable()`][executable] parameters may be provided.

   * {Rust}

   ```gn
   import("//src/sys/build/components.gni")

   fuchsia_rust_component("my-component") {
     deps = [ ... ]
   }

   # Alternatively, wrap an existing rustc_binary
   rustc_binary("my_rust_binary") { ... }

   fuchsia_rust_component("my-component") {
     rustc_binary = ":my_rust_binary"
   }
   ```

   See also: [`fuchsia_rust_component.gni`](/src/sys/build/fuchsia_rust_component.gni)

   Additional [`rustc_binary()`][rustc-binary] parameters may be provided.

Support for additional languages may be added upon request.

Note the following details:

*   These templates fuse the roles of the target that defines the executable
    program with that of `fuchsia_component()`. A `fuchsia_package()` or
    `fuchsia_test_package()` target is still required.
*   The executable will be packaged under the path `bin/`, and named after the
    target or `output_name` if it is defined.

### Unit tests {#unit-tests}

Since unit tests are very common, simplified templates are provided to define
them.

   * {C++}

   ```gn
   import("//src/sys/build/components.gni")

   fuchsia_cpp_unittest("my-unittest") {
     sources = [ "my_unittest.cc" ]
     deps = [ ... ]
   }
   ```

   See also: [`fuchsia_cpp_unittest.gni`](/src/sys/build/fuchsia_cpp_unittest.gni)

   Additional [`executable()`][executable] parameters may be provided.

   * {Rust}

   ```gn
   import("//src/sys/build/components.gni")

   fuchsia_rust_unittest("my-unittest") {
     deps = [ ... ]
   }

   # Alternatively, wrap an existing rustc_test
   rustc_library("my_rust_lib") {
     ...
     with_unit_tests = true
   }

   fuchsia_rust_unittest("my-unittest") {
     # rustc_binary and rustc_library generate an "_test" test target
     rustc_test = ":my_rust_lib_test"
     output_name = "my_rust_lib_lib_test"
   }
   ```

   See also: [`fuchsia_rust_unittest.gni`](/src/sys/build/fuchsia_rust_unittest.gni)

   Additional [`rustc_test()`][rustc-test] parameters may be provided.

Support for additional languages may be added upon request.

Note the following details:

*   These templates fuse the roles of the target that defines the executable
    program with that of `fuchsia_component()` and `fuchsia_test_package()`.
*   The executable will be packaged under the path `test/`, and named after the
    target or `output_name` if it is defined.
*   In the examples above, the targets don't specify a component manifest. One
    is then generated for them. The generated manifest requests very trivial
    capabilities for the component that are typically sufficient to run a
    "pure" unit test that exercises algorithms or business logic.
*   In order to provide a component manifest, specify a `manifest` parameter
    with a path to the manifest file.
*   One way to find the generated component manifest file is as follows:
    ```bash
    fx gn outputs out/default <unittest_target>_generated_manifest
    ```
    To print it directly:
    ```bash
    cat out/default/`fx gn outputs out/default <unittest_target>_generated_manifest`
    ```
    This command will not work if `manifest` is provided (i.e. there is no
    generated manifest). Instead an error will be printed stating that the
    target was not found.

### Additional packaged resources

In the [example above](#defining) you saw the use of `resources` in a component
definition. The same syntax applies to all component templates. Resources
included in a component definition will be included in any package that depends
on that component. Any number of resources can be specified in a component
target definition.

Sometimes it's useful to define resources outside of a component definition. For
instance, you may want to define resources and allow multiple components to
depend on them. For this, use
[`fuchsia_resources.gni`](/src/sys/build/fuchsia_resources.gni). For instance:

{# Disable variable substition to avoid {{ being interpreted by the template engine #}
{% verbatim %}

```gn
import("//src/sys/build/components.gni")

fuchsia_resources("roboto_family") {
  sources = [
    "Roboto-Black.ttf",
    "Roboto-Bold.ttf",
    "Roboto-Light.ttf",
    "Roboto-Medium.ttf",
    "Roboto-Regular.ttf",
    "Roboto-Thin.ttf",
  ]
  destination = "data/fonts/{{source_file_part}}"
}
```

{# Re-enable variable substition #}
{% endverbatim %}

In the example above, six files are provided to be packaged under `data/fonts/`,
producing the paths `data/fonts/Roboto-Black.ttf`,
`data/fonts/Roboto-Bold.ttf`, etc'. The format for `destination` accepts [GN
source expansion placeholders][source-expansion-placeholders].

## Troubleshooting

### Listing the contents of a package

Packages are described by a package manifest, which is a text file where every
line follows this structure:

```
<packaged-path>=<source-file-path>
```

To find the package manifest for a `fuchsia_package()` or
`fuchsia_test_package()` target, use the following command:

```bash
fx gn outputs out/default <package_target>_manifest
```

`<package_target` is a fully-qualified target name, i.e. in the form
`//path/to/your:target`.

Combine this with another command to print the package manifest:

```bash
cat out/default/`fx gn outputs out/default <package_target>_manifest`
```

See also:

*   [Working with packages][working-with-packages]
*   [pm]

## Migrating from legacy `package()`

The example below demonstrates a migration from the legacy
[`package()`](/build/package.gni) template to the new
[`fuchsia_package()`](/src/sys/build/fuchsia_package.gni) & friends.
The example is adapted from
[`//src/sys/timekeeper/BUILD.gn`](/src/sys/timekeeper/BUILD.gn).

### Pre-migration {#pre-migration}

import("//build/config.gni")
import("//build/package.gni")
import("//build/rust/rustc_binary.gni")

rustc_binary("bin") {
  name = "timekeeper"
  edition = "2018"
  with_unit_tests = true
  deps = [ ... ]
}

config_data("timekeeper_config") {
  for_pkg = "sysmgr"
  outputs = [ "timekeeper.config" ]
  sources = [ "service.config" ]
}

package("timekeeper") {
  meta = [
    {
      path = "meta/service.cmx"
      dest = "timekeeper.cmx"
    },
  ]
  deps = [
    ":bin",
    ":timekeeper_config",
  ]
  binaries = [
    {
      name = "timekeeper"
    },
  ]
}

test_package("timekeeper_bin_test") {
  deps = [ ":bin_test" ]
  tests = [
    {
      name = "timekeeper_bin_test"
      environments = basic_envs
    },
  ]
  resources = [
    {
      path = "test/y2k"
      dest = "y2k"
    },
    {
      path = "test/end-of-unix-time"
      dest = "end-of-unix-time"
    },
  ]
}

group("tests") {
  testonly = true
  deps = [ ":timekeeper_bin_test" ]
}

### Post-migration {#post-migration}

import("//build/config.gni")
import("//build/rust/rustc_binary.gni")
import("//src/sys/build/components.gni")

rustc_binary("bin") {
  name = "timekeeper"
  edition = "2018"
  with_unit_tests = true
  deps = [ ... ]
}

config_data("timekeeper_config") {
  for_pkg = "sysmgr"
  outputs = [ "timekeeper.config" ]
  sources = [ "service.config" ]
}

fuchsia_component("service") {
  component_name = "timekeeper"
  manifest = "meta/service.cmx"
  deps = [ ":bin" ]
}

fuchsia_package("timekeeper") {
  components = [ ":service" ]
}

fuchsia_unittest_package("timekeeper-unittests") {
  executable_name = "timekeeper"
  manifest = "meta/unittests.cmx"
  deps = [ ":bin_test" ]
  resources = [
    {
      source = "test/y2k"
      destination = "data/y2k"
    },
    {
      source = "test/end-of-unix-time"
      destination = "data/end-of-unix-time"
    },
  ]
}

### Migration considerations

*   Targets that generate executables or data files are not expected to change
    in a migration.
*   Names for packages are expected to have dashes ("-") instead of underscores
    ("_"). The same is not required for components, though it's recommended for
    consistency.
*   Previously, `meta/service.cmx` was given the destination `"timekeeper.cmx"`
    which placed it in `meta/timekeeper.cmx`. With `fuchsia_component()`, the
    given manifest is automatically renamed per the component name
    (`"timekeeper"`) and `meta/` is prepended. As a result, the launch URL for
    the timekeeper component remains the same:
    `fuchsia-pkg://fuchsia.com/timekeeper#meta/timekeeper.cmx`
*   Additional resources (in this case, the data asset files used in the test
    such as the `test/y2k` file) are included in a similar fashion. However
    their destination path is a full packaged path, whereas before it would have
    `"data/"` automatically prepended to it. In both cases, the data file can
    be read by the test at runtime from the paths `"/pkg/data/y2k"` and
    `"/pkg/data/end-of-unix-time"`.
*   Optionally `destination` can be omitted, in which case it defaults to the
    short name of the source file. In the example above the full paths would
    then be `"/pkg/y2k"` and `"/pkg/end-of-unix-time"`.

### Unsupported features

Note that some features of `package()` are unsupported moving forward. If your
package depends on them then at this time it cannot be migrated to the new
templates. These unsupported features include:

*   Legacy `shell` binaries (deprecated global `/bin` directory)
*   Marking a test as disabled. This should instead be done for instance by
    changing the test source code to disable specific test cases or the entire
    test suite.
*   Legacy `drivers` (deprecated global `/driver` directory)
*   Legacy `loadable_modules` and `libraries` (deprecated global `/lib`
    directory)

[cml-format]: /docs/concepts/components/component_manifests.md
[cmx-format]: /docs/concepts/storage/component_manifest.md
[executable]: https://gn.googlesource.com/gn/+/master/docs/reference.md#func_executable
[fx-test]: https://www.fuchsia.dev/reference/tools/fx/cmd/test.md
[glossary-component]: /docs/glossary.md#component
[glossary-component-instance]: /docs/glossary.md#component-instance
[glossary-component-manifest]: /docs/glossary.md#component-manifest
[glossary-fuchsia-pkg-url]: /docs/glossary.md#fuchsia-pkg-url
[glossary-gn]: /docs/glossary.md#gn
[glossary-package]: /docs/glossary.md#fuchsia-package
[package-name]: /docs/concepts/packages/package_url.md#package-name
[pm]: /src/sys/pkg/bin/pm/README.md
[rustc-binary]: /build/rust/rustc_binary.gni
[rustc-test]: /build/rust/rustc_test.gni
[source-code-layout]: /docs/concepts/source_code/layout.md
[source-expansion-placeholders]: https://gn.googlesource.com/gn/+/master/docs/reference.md#placeholders
[working-with-packages]: /docs/development/sdk/documentation/packages.md
