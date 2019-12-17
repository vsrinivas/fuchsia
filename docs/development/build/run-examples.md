
# Run the examples

While exploring the source code, you may have noticed the `examples` directory.
This guide will show you how to build Fuchsia to include some examples and then
run them on the device.

## Explore the hello_world example

Open the [`examples/hello_world/BUILD.gn`](/examples/hello_world/BUILD.gn) file.

If you aren't familiar with GN, take a look at the
[introductory presentation](https://docs.google.com/presentation/d/15Zwb53JcncHfEwHpnG_PoIbbzQ3GQi_cpujYwbpcbZo/view#slide=id.g119d702868_0_12) (slides 5-16 cover the core concepts, and
slides 17-21 describe debugging commands) or [docs](https://gn.googlesource.com/gn/+/master/docs/).
In short, GN is a meta build system. Its output files serve as inputs to
[Ninja](https://ninja-build.org/), the actual build system.

In this file, the `hello_world` target is a group containing other dependencies,
notably `cpp` and `rust`. Using this target will build both of them.

```gn
group("hello_world") {
  testonly = true
  deps = [
    ":tests",
    "cpp",
    "rust",
  ]
}
```

Note: You can look at the [`build/package.gni`](/build/package.gni) file to learn
more about how Fuchsia packages are defined by GN.

This example outputs `Hello, world!` and is written in both C++ and Rust. Each
language-dependent directory has its own `BUILD.gn` file that defines a package
for the specific example, as well as a `meta` subdirectory with `.cmx` files.

The `.cmx` file is known as a
[component manifest](/docs/glossary.md#component-manifest) and describes how to run
the application on Fuchsia as a [component](/docs/glossary.md#component). This is
the proper way to create a [Fuchsia package](/docs/glossary.md#fuchsia-package).

You run a Fuchsia component by referencing its
[Fuchsia package URI](/docs/glossary.md#fuchsia-pkg-url). To run one of the
examples:

1.  Make sure `fx serve` is running in a terminal window. If it's not running, start it:

    ```sh
    fx serve
    ```

1.  In another terminal, run:

    ```sh
    fx shell run fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
    ```

This should fail with a message stating the package was not found.

Note: If it succeeds and prints "Hello World!", then your current fx target
includes these examples already. You may need to modify your target and repave
the device, then return to this page to continue.

## Set the build to include examples

You can include the examples in the build, but you need to determine where they
will be included:

*   Base: Packages that are included in paving images produced by the build.
    They are included in over-the-air updates and are always updated as a
    single unit.

*   Cache: Packages that are included in paving images, but are not included in
    over-the-air system updates. These packages can be updated at any time that
    updates are available.

*   Universe: Packages that are additional optional packages that can be
    fetched and run on-demand, but are not pre-baked into any paving images.

(For more information, see [fx workflows](/docs/development/build/fx.md).)

To include this package in Universe so it can be fetched on-demand, use the
`--with` flag when setting the product and board:

```sh
fx set ... --with //examples/hello_world
fx build
```

You now have a build that includes the examples.

## Run the examples

1.  Make sure `fx serve` is running in a terminal window. If it's not running, start it:

    ```sh
    fx serve
    ```

1.  In another terminal, run:

    ```sh
    fx shell run fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
    ```

You should see the following output:

```uglyprint
Hello, World!
```

Important: If `fx serve` is not running, you should get an error from
the device (for example,
`fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx: not
found`).

The `run` command can expand a string to a URI if the string only matches one
component in your product configuration:

```sh
fx shell run hello_world_cpp
```

If there are multiple matches, the command will list them for you to choose
from:

```sh
fx shell run hello
```

```uglyprint
fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
fuchsia-pkg://fuchsia.com/hello_world_rust#meta/hello_world_rust.cmx
Error: "hello" matched multiple components.
```

You can explore what components are in your product configuration using the
`locate` command.

*   Find your favorite component.

    ```
    fx shell locate hello_world_cpp
    ```

*   Find all runnable components.

    ```
    fx shell locate --list cmx
    ```

*   Find multiple test components.

    ```
    fx shell locate --list test
    ```
