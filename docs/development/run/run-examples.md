# Run an example component

This guide shows you how to build Fuchsia to include an example package
from Fuchsia source's `//examples` directory and run its component
on your Fuchsia device.

Note: This guide is specific to components v1 and uses 
[component manifests](/docs/concepts/components/v1/component_manifests.md).

## Exploring the example Fuchsia package {#exploring-the-example-fuchsia-package}

Open the [`examples/hello_world/BUILD.gn`](/examples/hello_world/BUILD.gn) file.

This example, written in both C++ and Rust, prints `Hello, world!`. Each
language-dependent directory has the following:

*  A [`BUILD.gn`](#build-gn) file that defines its [Fuchsia package](#fuchsia-package).
*  A `meta` subdirectory with [component manifests](#component-manifest) (`.cmx`) files.

### BUILD.gn {#build-gn}

Generate Ninja (GN) is a meta build system. Output files from GN serve as inputs to
[Ninja](https://ninja-build.org/){:.external}, the actual build system.
If you aren't familiar with GN, see
[Introduction to GN](/docs/concepts/build_system/intro.md).

In the [`examples/hello_world/BUILD.gn`](/examples/hello_world/BUILD.gn) file,
the `hello_world` target is a group containing other dependencies,
notably `cpp` and `rust`. Therefore, this target builds both of them:

```none
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
    optional packages are fetched and run on-demand.


## Include the example package in your Fuchsia image {#include-the-example-package-in-your-fuchsia-image}

To include the example package in Universe (so that it can be fetched on-demand),
use the `--with` flag when setting your product and board environment:

```posix-terminal
fx set <PRODUCT>.<ARCH> --with //examples/hello_world
```

For a Fuchsia device, the recommended minimum build configuration is the following: 

```posix-terminal
fx set core.x64 --with //examples/hello_world
```

In this example, `core` is a product with a minimal feature set, which includes
common network capabilities, and `x64` refers to the x64 architecture.

```posix-terminal
fx set core.qemu-x64 --with //examples/hello_world
```

See [Configure a build](/docs/development/build/fx.md#configure-a-build) for
more options.

Once you have set your build configuration, build Fuchsia with the following
command: 

```posix-terminal
fx build
```

You now have a build that includes the example package in Universe.

## Run the example component {#run-the-example-component}

To run a Fuchsia component, use its
[Fuchsia package URL](/docs/glossary.md#fuchsia-pkg-url) as an argument
to the `fx shell run` command:

1.  Open a terminal and run `fx serve`:

    ```posix-terminal
    fx serve
    ```

1.  Open another terminal and run the example component:

    ```posix-terminal
    fx shell run fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
    ```

This command prints the following output:

```none
Hello, World!
```

If `fx serve` is not running, the command prints an error message from
the device or emulator. 

If `fx serve` is running, but the package is not found,
then [try going through these steps again](#include-the-example-package-in-your-fuchsia-image),
rebuilding your Fuchsia image
to include this package and repaving it to the device.

### Run the example component using a simple string {#run-the-example-component-using-a-simple-string}

The `fx shell run` command can match a string to a package URL
if the string is only mapped to one component
in your product configuration. For example:

```posix-terminal
$ fx shell run hello_world_cpp.cmx
```

If multiple matches exist, the command prints the list of matches:

```none
$ fx shell run hello_world
fuchsia-pkg://fuchsia.com/hello_world_cpp_tests#meta/hello_world_cpp_unittests.cmx
fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
Error: "hello_world" matched multiple components.
```

### Explore the components in your product configuration {#explore-components-in-product-configuration}

You can explore what components are in your product configuration using the
`locate` command.

*   Find your favorite component:

    ```posix-terminal
    fx shell locate hello_world_cpp.cmx
    ```

*   Find all runnable components:

    ```posix-terminal
    fx shell locate --list cmx
    ```

*   Find multiple test components:

    ```posix-terminal
    fx shell locate --list test
    ```
