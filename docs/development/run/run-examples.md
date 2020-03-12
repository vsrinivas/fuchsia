
# Run an example component

This guide shows you how to build Fuchsia to include an example package
from Fuchsia source's `//examples` directory and run its component
on your Fuchsia device.

## Examine a Fuchsia package {#examine-a-fuchsia-package}

Open the [`examples/hello_world/BUILD.gn`](/examples/hello_world/BUILD.gn) file.

This example, written in both C++ and Rust, prints `Hello, world!`. Each
language-dependent directory has the following:

*  A [`BUILD.gn`](#build-gn) file that defines its [Fuchsia package](#fuchsia-package).
*  A `meta` subdirectory with [component manifests](#component-manifest) (`.cmx`) files.

### BUILD.gn {#build-gn}

In short, GN is a meta build system. Output files from GN serve as inputs to
[Ninja](https://ninja-build.org/){:.external}, the actual build system.
If you aren't familiar with GN, see
[Introduction to GN](/docs/concepts/build_system/intro.md).

In the example package's top level `BUILD.gn` file,
the `hello_world` target is a group containing other dependencies,
notably `cpp` and `rust`. Therefore, this target builds both of them:

```uglyprint
group("hello_world") {
  testonly = true
  deps = [
    ":tests",
    "cpp",
    "rust",
  ]
}
```

To learn more about how GN defines Fuchsia packages,
see the [`build/package.gni`](/build/package.gni) file.

### Component manifest {#component-manifest}

A `.cmx` file, known as a
[component manifest](/docs/glossary.md#component-manifest), describes how to run
an application on Fuchsia as a [component](/docs/glossary.md#component). In
other words, a component manifest creates a [Fuchsia package](/docs/glossary.md#fuchsia-package).

### Fuchsia package {#fuchsia-package}

To include a package in your Fuchsia image, you have the following options:

*   Base: Packages that are produced by build and included in paving images.
    These packages are included in over-the-air updates and are always updated as a
    single unit.

*   Cache: Packages that are included in paving images, but are not included in
    over-the-air system updates. These packages can be updated at any time
    when updates are available.

*   Universe: Packages that are not included in paving image. These
    optional packages are fetched and run on-demand,


## Include the example package in your Fuchsia image {#include-the-example-package-in-your-fuchsia-image}

To include the example package in Universe (so that it can be fetched on-demand),
use the `--with` flag when setting your product and board environment:

```sh
fx set <PRODUCT>.<ARCH> --with //examples/hello_world
fx build
```

For more information on setting up `fx`, see [fx workflows](/docs/development/build/fx.md).

You now have a build that includes the example package in Universe.

## Run the example component {#run-the-example-component}

To run a Fuchsia component, use its
[Fuchsia package URL](/docs/glossary.md#fuchsia-pkg-url) as an argument
to the `fx shell run` command:

1.  Open a terminal and run `fx serve`:

    ```sh
    fx serve
    ```

1.  Open another terminal and run the example component:

    ```sh
    fx shell run fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
    ```

This command prints the following output:

```uglyprint
Hello, World!
```

If `fx serve` is not running, the command prints an error message from
the device:

```uglyprint
fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx: not found.
```

If `fx serve` is running, but the package is not found,
then try rebuilding your Fuchsia image to include this package
and repaving it to the device. See
[Include the example package in your Fuchsia image](#include-the-example-package-in-your-fuchsia-image)
for details.

### Run the example component using a simple string {#run-the-example-component-using-a-simple-string}

The `fx shell run` command can match a string to a package URL
if the string is only mapped to one component
in your product configuration. For example:

```uglyprint
$ fx shell run hello_world_cpp.cmx
```

If multiple matches exist, the command prints the list of matches:

```uglyprint
$ fx shell run hello_world
fuchsia-pkg://fuchsia.com/hello_world_cpp_tests#meta/hello_world_cpp_unittests.cmx
fuchsia-pkg://fuchsia.com/hello_world_rust_tests#meta/hello_world_rust_bin_test.cm
fuchsia-pkg://fuchsia.com/hello_world_rust_tests#meta/hello_world_rust_bin_test.cmx
fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx
fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cm
Error: "hello_world" matched multiple components
```

You can explore what components are in your product configuration using the
`locate` command.

*   Find your favorite component:

    ```
    fx shell locate hello_world_cpp
    ```

*   Find all runnable components:

    ```
    fx shell locate --list cmx
    ```

*   Find multiple test components:

    ```
    fx shell locate --list test
    ```
