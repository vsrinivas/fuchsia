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
[URL][glossary-component-url]. Typically components are launched by specifying
their package names and path to their component manifest in the package, using
the **[`fuchsia-pkg://` scheme][glossary-fuchsia-pkg-url]**.

## GN templates

[GN][glossary-gn] is the meta-build system used by Fuchsia. Fuchsia extends GN
by defining templates. Templates provide a way to add to GN's built-in target
types. Below we will review various GN templates that can be used to define
packages, components, and their tests.

### Defining components, packages, and tests using GN templates {#defining}

Below is a hypothetical package containing one component that runs a C++
program and a data file. The example uses the following templates:

*   [`fuchsia_component.gni`](/src/sys/build/fuchsia_component.gni)
*   [`fuchsia_package.gni`](/src/sys/build/fuchsia_package.gni)

```gn
import("//src/sys/build/components.gni")

executable("my_program") {
  sources = [ "my_program.cc" ]
}

fuchsia_component("my-component") {
  manifest = "meta/my_program.cmx"
  deps = [ ":my_program" ]
}

fuchsia_package("my-package") {
  deps = [ ":my-component" ]
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
    includes all templates related to packages, components, and testing them.
*   This example defines an `executable()`, which is used to build a C++
    program. This is for illustrative purposes - a component can launch all
    sorts of programs.
*   This example defines a `fuchsia_component()` which depends on the
    `executable()`. The component definition attaches a manifest, which
    references the executable to be launched under the given package path
    `bin/my_program`.
    See:
    [finding paths for built executables](#finding-paths-for-built-executables).
*   The manifest must be either a `.cmx` file in [cmx format][cmx-format] or a
    `.cml` file in [cml format][cml-format].
*   The destination path for the manifest is not specified, but rather
    inferred from the component's name. In this example, the manifest path will
    be `meta/my-component.cmx`.
*   [By convention][package-name] package names contain dashes (`-`) but not
    underscores (`_`), and the component name above follows suit. \
    Both the component and package names are derived from their target names.
    They both take an optional `component_name` and `package_name` parameter
    respectively as an override. \
    In the example above, these names come together to form the URL for
    launching the component:
    `fuchsia-pkg://fuchsia.com/my-package#meta/my-component.cmx`.

### Language-specific component examples {#language-specific-component-examples}

Below you'll find basic examples for defining a package with a single component
that launches a program in a variety of commonly used languages. The referenced
source files and component manifest are assumed to be present in the specified
paths.

   * {C++}

   ```gn
   import("//src/sys/build/components.gni")

   executable("bin") {
     output_name = "my_program"
     sources = [ "main.cc" ]
   }

   fuchsia_component("my-component") {
     manifest = "meta/my-component.cmx"
     deps = [ ":bin" ]
   }

   fuchsia_package("my-package") {
     deps = [ ":my-component" ]
   }
   ```

   It's assumed that the file `meta/my-component.cmx`
   contains at least the following:

   ```json
   {
     "program": {
        "binary": "bin/my_program"
     }
   }
   ```

   * {Rust}

   ```gn
   import("//build/rust/rustc_binary.gni")
   import("//src/sys/build/components.gni")

   rustc_binary("bin") {
     output_name = "my_program"
   }

   fuchsia_component("my-component") {
     manifest = "meta/my-component.cmx"
     deps = [ ":bin" ]
   }

   fuchsia_package("my-package") {
     deps = [ ":my-component" ]
   }
   ```

   It's assumed that the file `meta/my-component.cmx`
   contains at least the following:

   ```json
   {
     "program": {
        "binary": "bin/my_program"
     }
   }
   ```

   * {Go}

   ```gn
   import("//build/go/go_binary.gni")
   import("//src/sys/build/components.gni")

   go_binary("bin") {
     output_name = "my_program"
   }

   fuchsia_component("my-component") {
     manifest = "meta/my-component.cmx"
     deps = [ ":bin" ]
   }

   fuchsia_package("my-package") {
     deps = [ ":my-component" ]
   }
   ```

   It's assumed that the file `meta/my-component.cmx`
   contains at least the following:

   ```json
   {
     "program": {
        "binary": "bin/my_program"
     }
   }
   ```

  * {Dart}

   ```gn
   import("//build/dart/dart_component.gni")
   import("//build/dart/dart_library.gni")
   import("//src/sys/build/components.gni")

   dart_library("lib") {
     package_name = "my_lib"
     sources = [ "main.dart" ]
   }

   dart_component("my-component") {
     manifest = "meta/my-component.cmx"
     deps = [ ":lib" ]
   }

   fuchsia_package("my-package") {
     deps = [ ":my-component" ]
   }
   ```

   It's assumed that the file `meta/my-component.cmx`
   contains at least the following:

   ```json
   {
     "program": {
        "data": "data/my_component"
     }
   }
   ```

   * {Flutter}

   ```gn
   import("//build/dart/dart_library.gni")
   import("//build/flutter/flutter_component.gni")
   import("//src/sys/build/components.gni")

   dart_library("lib") {
     package_name = "my_lib"
     sources = [ "main.dart" ]
   }

   flutter_component("my-component") {
     manifest = "meta/my-component.cmx"
     deps = [ ":lib" ]
   }

   fuchsia_package("my-package") {
     deps = [ ":my-component" ]
   }
   ```

   It's assumed that the file `meta/my-component.cmx`
   contains at least the following:

   ```json
   {
     "program": {
        "data": "data/my_component"
     }
   }
   ```

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
  deps = [
    "//src/lib/fxl/test:gtest_main",
    "//third_party/googletest:gtest",
  ]
}

fuchsia_component("my-test-component") {
  testonly = true
  manifest = "meta/my_test.cmx"
  deps = [ ":my_test" ]
}

executable("my_program_under_test") {
  sources = [ "my_program_under_test.cc" ]
}

fuchsia_component("my-other-component-under-test") {
  manifest = "meta/my_component_under_test.cmx"
  deps = [ ":my_program_under_test" ]
}

fuchsia_test_package("my-integration-test") {
  test_components = [ ":my-test-component" ]
  deps = [ ":my-other-component-under-test" ]
  test_spec = {
    environments = [ vim2_env ]
  }
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

An important limitation of `fuchsia_test_package()` is that any
`test_components` targets must be defined in the same `BUILD.gn` file as the
test package target. This is due to a [limitation in GN][gn-get-target-outputs].

It's possible to work around this limitation with an indirection through
`fuchsia_test()`. In one `BUILD.gn` file, define:

```gn
import("//src/sys/build/components.gni")

executable("my_test") {
  sources = [ "my_test.cc" ]
  testonly = true
  deps = [
    "//src/lib/fxl/test:gtest_main",
    "//third_party/googletest:gtest",
  ]
}

fuchsia_component("my-test-component") {
  testonly = true
  manifest = "meta/my_test.cmx"
  deps = [ ":my_test" ]
}

fuchsia_test("my-test-component-test") {
  package = "//path/to:fuchsia_package"
  component = ":my-test-component"
}
```

Then elsewhere, you can add the `fuchsia_component()` target to the `deps` of a
`fuchsia_package()` target, and add the `fuchsia_test()` target to a standard
`"tests"` group.

Dart and Flutter tests differ slightly in that they need to be built with a
`flutter_test_component()` which collects all of the test mains into a single
main invocation. The `flutter_test_component()` can then be used by the
`fuchsia_test_package()`.

```gn
import("//build/dart/dart_test_component.gni")
import("//build/flutter/flutter_test_component.gni")
import("//src/sys/build/components.gni")

flutter_test_component("my-flutter-test-component") {
  testonly = true
  manifest = "meta/my-flutter-test-component.cmx"
  sources = [ "foo_flutter_test.dart" ]
}

dart_test_component("my-dart-test-component") {
  testonly = true
  manifest = "meta/my-dart-test-component.cmx"
  sources = [ "foo_dart_test.dart" ]
}

fuchsia_test("my-test-component-test") {
  test_components = [
    ":my-dart-test-component",
    ":my-flutter-test-component"
  ]
}
```

### Unit tests {#unit-tests}

Since unit tests are very common, two simplified templates are provided:

* [`fuchsia_unittest_component.gni`](/src/sys/build/fuchsia_unittest_component.gni) defines a
  component to be run as a test, with the option to automatically generate a basic component
  manifest, that must then be included in a package.
* [`fuchsia_unittest_package.gni`](/src/sys/build/fuchsia_unittest_package.gni) defines a
  package with a single component to be run as a test, shorthand for a single
  `fucshia_unittest_component` target paired with a `fuchsia_test_package`.

#### Unit tests with manifests {#unit-tests-with-manifests}

The examples below demonstrate building a test executable and defining a
package and component for the test.

   * {C++}

   ```gn
   import("//src/sys/build/components.gni")

   executable("my_test") {
     sources = [ "test.cc" ]
     deps = [
       "//src/lib/fxl/test:gtest_main",
       "//third_party/googletest:gtest",
     ]
     testonly = true
   }

   fuchsia_unittest_package("my-test") {
     manifest = "meta/my_test.cmx"
     deps = [ ":my_test" ]
   }
   ```

   * {Rust}

   ```gn
   import("//build/rust/rustc_test.gni")
   import("//src/sys/build/components.gni")

   rustc_test("my_test") {}

   fuchsia_unittest_package("my-test") {
     manifest = "meta/my_test.cmx"
     deps = [ ":my_test" ]
   }
   ```

   * {Go}

   ```gn
   import("//build/go/go_test.gni")
   import("//src/sys/build/components.gni")

   go_test("my_test") {}

   fuchsia_unittest_package("my-test") {
     manifest = "meta/my_test.cmx"
     deps = [ ":my_test" ]
   }
   ```

The manifest file `meta/my_test.cmx` may look like this:

```json
{
    "program": {
        "binary": "bin/my_test"
    }
}
```

The above is a minimal valid manifest file for this test. In practice a test
might require additional capabilities, to be specified in its manifest.

The launch URL for the test will be
`fuchsia-pkg://fuchsia.com/my-test#meta/my-test.cmx`. It can be launched using
`fx test` followed by the launch URL, or followed by the GN target name.

#### Unit tests with _generated_ manifests

The examples above specify a manifest for the test. However, it's possible for
unit tests to not require any particular capabilities.

Below is an example for a test that performs ROT13 encryption and decryption.
The algorithm under test is pure logic that can be tested in complete
isolation. If we were to write a manifest for these tests, it would only
contain the test binary to be executed. In such cases, we can simply specify
the test executable path, and the template will generate the trivial manifest
for us.

   * {C++}

   ```gn
   import("//src/sys/build/components.gni")

   executable("rot13_test") {
     sources = [ "rot13_test.cc" ]
     deps = [
       "//src/lib/fxl/test:gtest_main",
       "//third_party/googletest:gtest",
     ]
     testonly = true
   }

   fuchsia_unittest_package("rot13-test") {
     executable_path = "bin/rot13_test"
     deps = [ ":rot13_test" ]
   }
   ```

   * {Rust}

   ```gn
   import("//build/rust/rustc_test.gni")
   import("//src/sys/build/components.gni")

   rustc_test("rot13_test") {}

   fuchsia_unittest_package("rot13-test") {
     executable_path = "bin/rot13_test"
     deps = [ ":rot13_test" ]
   }
   ```

   * {Go}

   ```gn
   import("//build/go/go_test.gni")
   import("//src/sys/build/components.gni")

   go_test("rot13_test") {}

   fuchsia_unittest_package("rot13-test") {
     executable_path = "bin/rot13_test"
     deps = [ ":rot13_test" ]
   }
   ```

The generated component manifest file can be found as follows:

```bash
fx gn outputs out/default <unittest_target>_generated_manifest
```

To print it directly:

```bash
fx build && cat out/default/`fx gn outputs out/default <unittest_target>_generated_manifest`
```

Note that `fx gn outputs` prints an output path, but the file at the path
may not exist or may be stale if you haven't built.

The launch URL for the test will be
`fuchsia-pkg://fuchsia.com/rot13-test#meta/rot13-test.cmx`. It can be launched
using `fx test` followed by the launch URL, or followed by the GN target name.

Finally, if `executable_path` references a file that is not present in the
package then a helpful error message will be printed at build time. For
example:

```
Error found in obj/src/path/to/your/target/target-unittest.cmx
program.binary="bin/target-unittest-bin" but bin/target-unittest-bin is not in
the package!

Did you mean "test/target-unittest-bin"?
```

#### Multiple unit tests in a single package

`fuchsia_unittest_component` can be used instead of `fuchsia_unittest_package` to include multiple
components in a single package. This can be useful to easily run all the test components an a
single package, e.g. with `fx test <package_name>`, rather than needing to type many separate
package names.

The example below creates a single test package `rot13-tests` that contains two separate test
components, `rot13-decoder-test` and `rot13-encoder-test`. Both tests can be run using `fx test
rot13-tests`, or individual tests can be run using either `fx test rot13-decoder-test` or the
full URL `fx test fuchsia-pkg://fuchsia.com/rot13-tests#meta/rot13-decoder-test.cmx`.

   * {C++}

   ```gn
   import("//build/rust/rustc_test.gni")
   import("//src/sys/build/components.gni")

   executable("rot13_decoder_test") {}

   executable("rot13_encoder_test") {}

   fuchsia_unittest_component("rot13-decoder-test") {
     executable_path = "bin/rot13_decoder_test"
     deps = [ ":rot13_decoder_test" ]
   }

   fuchsia_unittest_component("rot13-encoder-test") {
     executable_path = "bin/rot13_encoder_test"
     deps = [ ":rot13_encoder_test" ]
   }

   fuchsia_test_package("rot13-tests") {
     test_components = [
       ":rot13-decoder-test",
       ":rot13-encoder-test",
     ]
   }
   ```

   * {Rust}

   ```gn
   import("//build/rust/rustc_test.gni")
   import("//src/sys/build/components.gni")

   rustc_test("rot13_decoder_test") {}

   rustc_test("rot13_encoder_test") {}

   fuchsia_unittest_component("rot13-decoder-test") {
     executable_path = "bin/rot13_decoder_test"
     deps = [ ":rot13_decoder_test" ]
   }

   fuchsia_unittest_component("rot13-encoder-test") {
     executable_path = "bin/rot13_encoder_test"
     deps = [ ":rot13_encoder_test" ]
   }

   fuchsia_test_package("rot13-tests") {
     test_components = [
       ":rot13-decoder-test",
       ":rot13-encoder-test",
     ]
   }
   ```

   * {Go}

   ```gn
   import("//build/go/go_test.gni")
   import("//src/sys/build/components.gni")

   go_test("rot13_decoder_test") {}

   go_test("rot13_encoder_test") {}

   fuchsia_unittest_component("rot13-decoder-test") {
     executable_path = "bin/rot13_decoder_test"
     deps = [ ":rot13_decoder_test" ]
   }

   fuchsia_unittest_component("rot13-encoder-test") {
     executable_path = "bin/rot13_encoder_test"
     deps = [ ":rot13_encoder_test" ]
   }

   fuchsia_test_package("rot13-tests") {
     test_components = [
       ":rot13-decoder-test",
       ":rot13-encoder-test",
     ]
   }
   ```

## Additional packaged resources {#additional-packaged-resources}

In the examples above we've demonstrated that a `deps` path from a package to a
target that produces an executable ensures that the executable is included in
the package.

Sometimes there is the need to include additional files. Below we demonstrate
the use of the [`resource.gni`](/build/unification/zbi/resource.gni) template.

### Example: fonts

{# Disable variable substition to avoid {{ being interpreted by the template engine #}
{% verbatim %}

```gn
import("//src/sys/build/components.gni")

resource("roboto_family") {
  sources = [
    "Roboto-Black.ttf",
    "Roboto-Bold.ttf",
    "Roboto-Light.ttf",
    "Roboto-Medium.ttf",
    "Roboto-Regular.ttf",
    "Roboto-Thin.ttf",
  ]
  outputs = [ "data/fonts/{{source_file_part}}" ]
}

fuchsia_component("text-viewer") {
  ...
  deps = [
    ":roboto_family",
    ...
  ]
}
```

{# Re-enable variable substition #}
{% endverbatim %}

In the example above, six files are provided to be packaged under `data/fonts/`,
producing the paths `data/fonts/Roboto-Black.ttf`,
`data/fonts/Roboto-Bold.ttf`, etc'. The format for `destination` accepts [GN
source expansion placeholders][source-expansion-placeholders].

Then, a text viewer component is defined to depend on the fonts. In this
example, the text viewer implementation renders text with Roboto fonts. The
component can read the given fonts in its sandbox under the path
`/pkg/data/fonts/...`.

### Example: integration test with golden data

In this example we define a hypothetical service that minifies JSON files. The
service is said to receive a buffer containing JSON text, and returns a buffer
containing the same JSON data but with less whitespace. We present an
integration test where a test component acts as the client of the minifier
component, and compares the result for a given JSON file to be minified against
a known good result (or a "golden file").

{# Disable variable substition to avoid {{ being interpreted by the template engine #}
{% verbatim %}

```
fuchsia_component("minifier-component") {
  ...
}

fuchsia_package("minifier-package") {
  ...
}

resource("testdata") {
  sources = [
    "testdata/input.json",
    "testdata/input_minified.json",
  ]
  outputs = [ "data/{{source_file_part}}" ]
}

fuchsia_component("minifier-test-client") {
  testonly = true
  deps = [
    ":testdata",
    ...
  ]
  ...
}

fuchsia_test_package("minifier-integration-test") {
  test_components = [ ":minifier-test-client" ]
  deps = [ ":minifier-component" ]
}
```

{# Re-enable variable substition #}
{% endverbatim %}

Note that we place the `resource()` dependency on the test component. From the
build system's perspective the resource dependency could have been placed on
the test package and the same outcome would have been produced by the build.
However, it is a better practice to put dependencies on the targets that need
them. This way we could reuse the same test component target in a different
test package, for instance to test against a different minifier component, and
the test component would work the same.

## Troubleshooting {#troubleshooting}

### Listing the contents of a package {#listing-the-contents-of-a-package}

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

### Finding paths for built executables {#finding-paths-for-built-executables}

Executable programs can be built with various language-specific templates such
as `executable()`, `rustc_binary()`, `go_binary()` etc'. These templates are
responsible for specifying where in a package their output binaries should be
included. The details vary by runtime and toolchain configuration.

*   Typically the path is `bin/` followed by the target's name.
*   Typically if an `output_name` or `name` is specified, it overrides the
    target name.

Some rudimentary examples are given below:

   * {C++}

   ```gn
   # This will be packaged as `bin/rot13_encode`
   executable("rot13_encode") {
     sources = [ "main.cc" ]
   }
   ```

   * {Rust}

   ```gn
   # This will be packaged as `bin/rot13_encode`
   rustc_binary("rot13_encode") {}
   ```

   * {Go}

   ```gn
   # This will be packaged as `bin/rot13_encode`
   go_binary("rot13_encode") {}
   ```

In order to reference an executable in a component manifest, the author will
need to know its packaged path.

One way to find the packaged path for an executable is to make sure that the
target that builds the executable is in a package's `deps`, then follow the
above guide for [listing the contents of a
package](#listing-the-contents-of-a-package). The executable will be among the
listed contents of the package.

### Finding a [component's launch URL][glossary-component-url]

Component URLs follow this pattern:

```none
fuchsia-pkg://fuchsia.com/<package-name>#meta/<component-name>.<extension>
```

*   `<package-name>`: specified as `package_name` on the package target, which
    defaults to the target name.
*   `<component-name>`: specified as `component_name` on the component target,
    which defaults to the target name.
*   `<extension>`: based on the [component
    manifest][glossary-component-manifest] - `cmx` for cmx files, `cm` for cml
    files.

## Migrating from legacy `package()`

The example below demonstrates a migration from the legacy
[`package()`](/build/package.gni) template to the new
[`fuchsia_package()`](/src/sys/build/fuchsia_package.gni) & friends.
The example is adapted from
[`//src/sys/time/timekeeper/BUILD.gn`](/src/sys/time/timekeeper/BUILD.gn).

### Pre-migration {#pre-migration}

```
import("//build/config.gni")
import("//build/package.gni")
import("//build/rust/rustc_binary.gni")

rustc_binary("bin") {
  output_name = "timekeeper"
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
```

### Post-migration {#post-migration}

{# Disable variable substition to avoid {{ being interpreted by the template engine #}
{% verbatim %}

```
import("//build/config.gni")
import("//build/rust/rustc_binary.gni")
import("//src/sys/build/components.gni")

rustc_binary("bin") {
  output_name = "timekeeper"
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
  deps = [ ":service" ]
}

resource("testdata") {
  sources = [
    "test/y2k",
    "test/end-of-unix-time",
  ]
  outputs = [ "data/{{source_file_part}}" ]
}

fuchsia_unittest_package("timekeeper-unittests") {
  manifest = "meta/unittests.cmx"
  deps = [
    ":bin_test",
    ":testdata",
  ]
}
```

{# Re-enable variable substition #}
{% endverbatim %}

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
    such as the `test/y2k` file) are included in the unit test. Their
    destination path is a full packaged path, whereas before it would have had
    `data/` automatically prepended to it. In both cases, the data file can
    be read by the test at runtime from the paths `/pkg/data/y2k` and
    `/pkg/data/end-of-unix-time`.
*   If you're required to specify a packaged path such as the path to an
    executable in a manifest or a test definition, and you're not sure what the
    path is, then try your best guess and expect a helpful error message if
    your guess was not correct.

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
*   Marking a test as disabled. Instead, change the test source code to mark it
    as disabled, or comment out the disabled test component from the build file.
*   The [Component Index][component-index]. Components using the new templates
    cannot be launched using `run` followed by a fuzzy match with their launch
    URL. Components can still be launched using their full launch URL. Tests
    can still be launched with `fx test` followed by the short name of the
    test. See [fxbug.dev/55739][fxb-55739] for more details.

[cml-format]: /docs/concepts/components/v2/component_manifests.md
[cmx-format]: /docs/concepts/components/v1/component_manifests.md
[component-index]: /src/sys/component_index/component_index.gni
[executable]: https://gn.googlesource.com/gn/+/master/docs/reference.md#func_executable
[fx-test]: https://www.fuchsia.dev/reference/tools/fx/cmd/test.md
[fxb-55739]: https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=55739
[glossary-component]: /docs/glossary.md#component
[glossary-component-instance]: /docs/glossary.md#component-instance
[glossary-component-manifest]: /docs/glossary.md#component-manifest
[glossary-component-url]: /docs/glossary.md#component-url
[glossary-fuchsia-pkg-url]: /docs/glossary.md#fuchsia-pkg-url
[glossary-gn]: /docs/glossary.md#gn
[glossary-package]: /docs/glossary.md#fuchsia-package
[gn-get-target-outputs]: https://gn.googlesource.com/gn/+/master/docs/reference.md#func_get_target_outputs
[package-name]: /docs/concepts/packages/package_url.md#package-name
[pm]: /src/sys/pkg/bin/pm/README.md
[rustc-binary]: /build/rust/rustc_binary.gni
[rustc-test]: /build/rust/rustc_test.gni
[source-code-layout]: /docs/concepts/source_code/layout.md
[source-expansion-placeholders]: https://gn.googlesource.com/gn/+/master/docs/reference.md#placeholders
[working-with-packages]: /docs/development/sdk/documentation/packages.md
