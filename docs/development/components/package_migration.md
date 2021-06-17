# Migrating legacy package build rules {#legacy-package-migration}

Note: this migration is complete! The guide below is kept for archival
purposes.

This document provides instructions on migrating the component build rules in
your `BUILD.gn` files from the legacy [`package()`] template
to the new [`fuchsia_package()`](/build/components/fuchsia_package.gni).

For more details on using the new component build rules, see the
[Build components][build-components] developer guide.

## Migration examples

The examples below demonstrate some common migration scenarios.

### Simple `package()` example

This example is adapted from
[`//src/sys/component_index/BUILD.gn`](/src/sys/component_index/BUILD.gn).

* {Pre-migration}

  ```gn
  import("//build/config.gni")
  import("//build/package.gni")                              # <1>
  import("//build/rust/rustc_binary.gni")
  import("//build/test/test_package.gni")                    # <1>

  rustc_binary("component_index_bin") {                      # <2>
    name = "component_index"
    # Generate a ":bin_test" target for unit tests
    with_unit_tests = true
    edition = "2018"
    deps = [ ... ]
  }

  package("component_index") {                               # <3>
    deps = [ ":component_index_bin" ]

    binaries = [
      {
        name = "component_index"
      },
    ]

    meta = [
      {
        path = rebase_path("meta/component_index.cmx")       # <4>
        dest = "component_index.cmx"                         # <4>
      },
    ]

    resources = [ ... ]
  }

  test_package("component_index_tests") {                    # <5>
    deps = [ ":component_index_bin_test" ]

    tests = [
      {
        name = "component_index_bin_test"                    # <5>
        dest = "component_index_tests"                       # <5>
      },
    ]
  }
  ```

* {Post-migration}

  ```gn
  import("//build/config.gni")
  import("//build/rust/rustc_binary.gni")
  import("//build/components.gni")                   # <1>

  rustc_binary("component_index_bin") {                      # <2>
    name = "component_index"
    # Generate a ":bin_test" target for unit tests
    with_unit_tests = true
    edition = "2018"
    deps = [ ... ]
  }

  fuchsia_package_with_single_component("component_index") { # <3>
    manifest = "meta/component_index.cmx"                    # <4>
    deps = [ ":component_index_bin" ]
  }

  fuchsia_unittest_package("component_index_tests") {        # <5>
    deps = [ ":component_index_bin_test" ]
  }
  ```

The following key elements are called out in the code example above:

> 1.  Necessary imports are replaced by `//build/components.gni`.
> 2.  Targets that generate executables or data files are not expected to change
>     in a migration.
> 3.  The original `package()` declaration contains a single component manifest
>     (listed under `meta`). The `fuchsia_package_with_single_component()`
>     template can replace this, referencing the same manifest file.
> 4.  Under `package()`, the manifest is given a specific destination
>     (`"component_index.cmx"`) to place it in the final package. With the new
>     templates, the manifest is renamed according to the **target name**.
>     As a result, the launch URL for the component remains the same.
> 5.  For a simple test package that does not contain multiple test components,
>     the `fuchsia_unittest_package()` template replaces `test_package()`. A
>     basic test component manifest is automatically generated and
>     `meta/component_index_tests.cmx` is no longer needed.

### Complex `package()` example

This example is adapted from
[`//src/fonts/BUILD.gn`](/src/fonts/BUILD.gn).

* {Pre-migration}

  ```gn
  import("//build/package.gni")                            # <1>
  import("//build/rust/rustc_binary.gni")
  import("//build/test/test_package.gni")                  # <1>
  import("//src/fonts/build/fonts.gni")

  rustc_binary("font_provider") {                          # <2>
    name = "font_provider"
    # Generate a ":bin_test" target for unit tests
    with_unit_tests = true
    edition = "2018"

    deps = [ ... ]
    sources = [ ... ]
  }

  package("pkg") {
    package_name = "fonts"

    deps = [ ":font_provider" ]

    binaries = [
      {
        name = "font_provider"
      },
    ]
    meta = [                                               # <3>
      {
        path = rebase_path("meta/fonts.cmx")               # <3>
        dest = "fonts.cmx"                                 # <4>
      },
      {
        path = rebase_path("meta/fonts.cml")               # <3>
        dest = "fonts.cm"                                  # <4>
      },
    ]
  }

  test_package("font_provider_unit_tests") {
    deps = [ ":font_provider_test" ]

    v2_tests = [
      {
        name = "font_provider_bin_test"                    # <4>
      },
    ]
  }
  ```

* {Post-migration}

  ```gn
  import("//build/rust/rustc_binary.gni")
  import("//src/fonts/build/fonts.gni")
  import("//build/components.gni")                         # <1>

  rustc_binary("font_provider") {                          # <2>
    name = "font_provider"
    # Generate a ":bin_test" target for unit tests
    with_unit_tests = true
    edition = "2018"

    deps = [ ... ]
    sources = [ ... ]
  }

  fuchsia_component("font_provider_cm") {                  # <3>
    manifest = "meta/fonts.cml"
    component_name = "fonts"                               # <4>
    deps = [ ":font_provider" ]
  }

  fuchsia_component("font_provider_cmx") {                 # <3>
    manifest = "meta/fonts.cmx"
    component_name = "fonts"                               # <4>
    deps = [ ":font_provider" ]
  }

  fuchsia_package("pkg") {
    package_name = "fonts"
    deps = [
      ":font_provider_cm",                                 # <3>
      ":font_provider_cmx",                                # <3>
    ]
  }

  fuchsia_component("font_provider_unit_tests_cmp") {
    testonly = true
    manifest = "meta/font_provider_bin_test.cml"
    component_name = "font_provider_bin_test"              # <4>
    deps = [ ":font_provider_test" ]
  }

  fuchsia_test_package("font_provider_unit_tests") {
    test_components = [ ":font_provider_unit_tests_cmp" ]
  }
  ```

The following key elements are called out in the code example above:

> 1.  Necessary imports are replaced by `//build/components.gni`.
> 2.  Targets that generate executables or data files are not expected to change
>     in a migration.
> 3.  If a `package()` includes multiple distinct components using the `meta`
>     field, each one must be broken out into a separate `fuchsia_component()`
>     and collected together in the `fuchsia_package()` using `deps`.
> 4.  Each `fuchsia_component()` uses the `component_name` field to map the
>     manifest destination in the final package. Without this, they are placed
>     according to the **target name**, which affects the launch URL of the
>     component.
>     This is true for both `fuchsia_package()` and `fuchsia_test_package()`.

Note: The new build templates allow targets that produce files, such as
`executable()`, to decide which files they produce and where the targets place
these files. This may affect the packaged path to binaries in your manifest or
test definition after migrating. If you encounter build-time errors you are
unable to resolve, see [Troubleshooting](#troubleshooting).

## Test package considerations

The example below highlights some key differences between the legacy
[`test_package()`] template and the new
[`fuchsia_test_package()`](/build/components/fuchsia_test_package.gni).

* {Pre-migration}

  ```gn
  import("//build/package.gni")                            # <1>
  import("//build/test/test_package.gni")                  # <1>

  executable("foo_bin_test") { ... }

  test_package("foo_tests") {                              # <1>
    deps = [ ":foo_bin_test" ]                             # <2>

    tests = [                                              # <3>
      {
        name = "foo_test"                                  # <2>
        log_settings = {
          max_severity = "ERROR"
        }
      }
    ]
  }
  ```

* {Post-migration}

  ```gn
  import("//build/components.gni")                         # <1>

  executable("foo_bin_test") { ... }

  fuchsia_component("foo_test") {                          # <2>
    testonly = true
    manifest = "meta/foo_test.cmx"
    deps = [ ":foo_bin_test" ]
  }

  fuchsia_test_package("foo_tests") {                      # <1>
    test_components = [ ":foo_test" ]                      # <2>

    test_specs = {                                         # <3>
      log_settings = {
        max_severity = "ERROR"
      }
    }
  }
  ```

The following key elements are called out in the code example above:

> 1.  Replace necessary imports with `//build/components.gni` and rename
>     `test_package()` to `fuchsia_test_package()`.
> 2.  Create a `fuchsia_component()` to encapsulate the test components previously
>     added with the `tests` field. Reference the components in the package with
>     the new `test_components` field.
>
>     Note: A `test_package()` typically sets the packaged path for binaries to
>     `test/`, while the new build rules let the executables define this and they
>     typically use `bin/`. This may affect the packaged path to binaries in your
>     test definition after migrating. If you encounter build-time errors you are
>     unable to resolve, see [Troubleshooting](#troubleshooting).
>
> 3.  Both template families support test specifications, such as restricting to
>     specific [test environments][test-environments] or
>     [restricting log severity][restrict-log-severity].
>
>     Note: With the new templates, the `test_specs` apply to all tests in the package.
>     See [test packages](#test-packages) for more examples.

## Remove legacy allowlist

The `deprecated_package` group in [`//build/BUILD.gn`](/build/BUILD.gn) contains
an allowlist of build files still using the legacy `package()` template.
Once you have successfully migrated your build files to the new templates,
remove the affected lines from the group. Removing the allowlist entries prevents
future changes from re-introducing uses of the legacy templates.

For example, if you migrated the files under [`//src/fonts`](/src/fonts) to the
new templates, you would find and remove all the related files paths in
[`//build/BUILD.gn`](/build/BUILD.gn):

```gn
group("deprecated_package") {
  ...
  visibility += [
    ...
    "//src/fonts/*",
    "//src/fonts/char_set/*",
    "//src/fonts/font_info/*",
    "//src/fonts/manifest/*",
    "//src/fonts/offset_string/*",
    "//src/fonts/tests/integration/*",
    "//src/fonts/tests/smoke/*",
    ...
  ]
}
```

## Troubleshooting {#troubleshooting}

### Listing the contents of a package {#listing-the-contents-of-a-package}

Packages are described by a package manifest, which is a text file where every
line follows this structure:

```
<packaged-path>=<source-file-path>
```

To find the package manifest for a `fuchsia_package()` or
`fuchsia_test_package()` target, use the following command:

<pre class="prettyprint">
<code class="devsite-terminal">fx gn outputs out/default <var>package target</var>_manifest</code>
</pre>

The package target is a fully-qualified target name, i.e. in the form
`//path/to/your:target`.

Combine this with another command to print the package manifest:

<pre class="prettyprint">
<code class="devsite-terminal">cat out/default/$(fx gn outputs out/default <var>package target</var>_manifest)</code>
</pre>

See also:

*   [Working with packages][working-with-packages]
*   [Fuchsia Package Manager][pm]

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
   # Binary is packaged as `bin/rot13_encode`
   executable("rot13_encode") {
     sources = [ "main.cc" ]
   }
   ```

   * {Rust}

   ```gn
   # Binary is packaged as `bin/rot13_encode`
   rustc_binary("rot13_encode") {}
   ```

   * {Go}

   ```gn
   # Binary is packaged as `bin/rot13_encode`
   go_binary("rot13_encode") {}
   ```

In order to reference an executable in a component manifest, the author needs
to know its packaged path.

One way to find the packaged path for an executable is to make sure that the
target that builds the executable is in a package's `deps`, then follow
[listing the contents of a package](#listing-the-contents-of-a-package).
The executable is listed among the contents of the package.

### Finding a component's launch URL

Component URLs follow this pattern:

```none
fuchsia-pkg://fuchsia.com/<package-name>#meta/<component-name>.<extension>
```

*   `<package-name>`: specified as `package_name` on the package target, which
    defaults to the target name.
*   `<component-name>`: specified as `component_name` on the component target,
    which defaults to the target name.
*   `<extension>`: based on the [component manifest][glossary.component-manifest];
    `cmx` for CMX files, `cm` for CML files.

## Legacy features

The following special attributes are supported by the legacy `package()` template:

*   `binaries`
*   `drivers`
*   `libraries`
*   `loadable_modules`

These are used with special syntax, which determines how the files that certain
targets produce are packaged.
For instance the `libraries` attribute installs resources in a special `lib/` directory,
`drivers` are installed in `drivers/`, etc'.
The legacy syntax looks like this:

```
package("my_driver_package") {
  deps = [ ":my_driver" ]

  drivers = [
    {
      name = "my_driver.so"
    },
  ]
}
```

This special treatment is not necessary with the new templates. Simply add the
necessary target to `deps = [ ... ]` and the packaging is done automatically.

```
fuchsia_component("my_driver_component") {
  deps = [ ":my_driver" ]
  ...
}

fuchsia_package("my_driver_package") {
  deps = [ ":my_driver_component" ]
  ...
}
```

Additionally, legacy `package()` supports the `resources` attribute. This is
replaced by adding a dependency on a `resource()` target.
For more details, see [Provide data files to components][resource-data].

## Renaming files

The legacy `package()` template allowed developers to rename certain files that
are included in their package. For example, below we see an executable being
built and then renamed before its packaged so that its packaged under the path
`bin/foo_bin`.

```gn
import("//build/package.gni")

executable("bin") {
  ...
}

package("foo_pkg") {
  deps = [ ":bin" ]
  binaries = [
    {
      name = "bin"
      dest = "foo_bin"
    }
  ]
  meta = [
    {
      path = "meta/foo_bin.cmx"
      dest = "foo.cmx"
    }
  ]
}
```

The new templates allow targets that produce files, such as `executable()`
above, to decide which files they produce and where they're placed. This is
important because some targets produce multiple files, or might produce
different files based on the build configuration (for instance if building
for a different target architecture). In order to control the paths of
packaged files, developers should work with the templates for the targets
that produce those files. For instance:

```gn
import("//build/components.gni")

executable("bin") {
  output_name = "foo_bin"
  ...
}

fuchsia_component("foo_cmp") {
  deps = [ ":bin" ]
  manifest = "meta/foo_bin.cmx"
}

fuchsia_package("foo_pkg") {
  deps = [ ":foo_cmp" ]
}
```

## Shell binaries

The legacy `package()` template allowed developers to make a particular binary
in the package available to `fx shell`.

```gn
import("//build/package.gni")

# `fx shell echo Hello World` prints "Hello World"
executable("bin") {
  output_name = "echo"
  ...
}

package("echo") {
  binaries = [
    {
      name = "echo"
      dest = "echo"
      shell = true
    }
  ]
  deps = [ ":bin" ]
}
```

The new templates support this feature as follows:

```gn
import("//build/components.gni")

# `fx shell echo Hello World` prints "Hello World"
executable("bin") {
  output_name = "echo"
  ...
}

fuchsia_shell_package("echo") {
  deps = [ ":bin" ]
}
```

Note that in the `package()` example the binary is explicitly named "echo",
which is the same name that's used for its intrinsic name
(`output_name = "echo"`). The new templates don't have this renaming behavior,
and instead let the target that produces the binary (`executable()` in this
case) decide the file name, as determined by the `output_name` specified (or the
executable target's name if `output_name` isn't specified).

This feature was left out intentionally.
Moving forward the use of legacy shell tools is discouraged.

## Go `grand_unified_binary`

"Grand unified binary" (GUB) is a single binary that merges together multiple Go
programs. The entry point to the combined program can identify which sub-program
the caller intends to run based on the filename of the invocation (`argv[0]`).
Therefore in order to include GUB in your package and invoke a sub-program the
common practice is to rename the binary.

The legacy `package()` template allowed developers to accomplish this as shown
below:

```gn
import("//build/go/go_library.gni")
import("//build/package.gni")

go_library("my_tool") {
  ...
}

package("tools") {
  deps = [
    "//src/go/grand_unified_binary",
  ]
  binaries = [
    {
      name = "my_tool"
      source = "grand_unified_binary"
    }
  ]
}
```

The new templates support this feature as follows:

```gn
import("//build/go/go_library.gni")
import("//src/go/grand_unified_binary/gub.gni")
import("//build/components.gni")

go_library("my_tool") {
  ...
}

grand_unified_binary("bin") {
  output_name = "my_tool"
}

fuchsia_package("tools") {
  deps = [ ":bin" ]
}
```

## Legacy component index (aka `fx run my_package`)

The legacy `package()` template supported a short-form syntax for launching legacy
v1 components in the legacy sys shell.

```gn
import("//build/package.gni")

executable("bin") {
  output_name = "echo"
  sources = [ "echo.cc" ]
}

package("echo") {
  deps = [ ":bin" ]
  binaries = [
    {
      name = "echo"
    },
  ]
  meta = [
    {
      path = "meta/echo.cmx"
      dest = "echo.cmx"
    },
  ]
}
```

```posix-terminal
fx run echo Hello World
```

This is also known as the [Component Index][component-index].

The new templates don't support this feature out of the box, but you can use the
full launch URL.

```posix-terminal
fx run fuchsia-pkg://fuchsia.com/echo#meta/echo.cmx Hello World
```

The plan is to deprecate the legacy shell and the legacy component index along
with it, but there is currently no concrete timeline for this deprecation. If
you'd like to keep the old behavior, you can do so with this special syntax:

```gn
import("//build/components.gni")
import("//src/sys/component_index/component_index.gni")

executable("bin") {
  output_name = "echo"
  sources = [ "echo.cc" ]
}

add_to_component_index("component_index") {
  package_name = "echo"
  manifest = "meta/echo.cmx"
}

fuchsia_package_with_single_component("echo") {
  deps = [
    ":bin",
    ":component_index",
  ]
  manifest = "meta/echo.cmx"
}
```

## Other unsupported features

Note that some features of `package()` are unsupported moving forward. If your
package depends on them then at this time it cannot be migrated to the new
templates. These unsupported features include:

*   Marking a test as disabled. Instead, change the test source code to mark it
    as disabled, or comment out the disabled test component from the build file.
*   `__deprecated_system_image`: the legacy approach to including a package in
    the system image is not supported moving forward. A solution is being
    prepared and will be available later in 2021.
    Nearly all existing uses of this legacy feature are done via the
    `driver_package()` wrapper, which currently cannot be migrated.

[build-components]: /docs/development/components/build.md
[component-index]: /src/sys/component_index/component_index.gni
[glossary.component-manifest]: /docs/glossary/README.md#component-manifest
[glossary.component-url]: /docs/glossary/README.md#component-url
[pm]: /src/sys/pkg/bin/pm/README.md
[resource-data]: /docs/development/components/data.md
[restrict-log-severity]: /docs/concepts/testing/logs.md#restricting_log_severity
[test-environments]: /docs/concepts/testing/environments.md
[working-with-packages]: /docs/development/idk/documentation/packages.md
