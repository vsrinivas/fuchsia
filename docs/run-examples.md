
# Run the examples

While exploring the source code, you may have noticed the `examples` directory.
This guide will show you how to build Fuchsia to include these examples and then
run them on the device.

## Explore the GN file

Go ahead and open the `BUILD.gn` file in the `examples` directory.

If you aren't familiar with GN, take a look at the
[introductory presentation](https://docs.google.com/presentation/d/15Zwb53JcncHfEwHpnG_PoIbbzQ3GQi_cpujYwbpcbZo/view#slide=id.g119d702868_0_12)
or [docs](https://gn.googlesource.com/gn/+/master/docs/). In short, GN is a meta
build system. Its output files serve as inputs to
[Ninja](https://ninja-build.org/), the actual build system.

In this file, you can see that the examples are designated as a named group.
This group names three targets as dependencies:

```gn
group("examples") {
  testonly = true
  deps = [
    "cowsay",
    "fortune",
    "hello_world",
  ]
}
```

You can find a directory for each of these dependencies. Each directory has its
own `BUILD.gn` file that defines a package for the specific example.

Note: You can look at the `build/package.gni` file to learn more about how
Fuchsia packages are defined by GN.

Let's take a look at each example.

### Cowsay

This sample takes a word and repeats it back from an ASCII-art cow.

To access a shell on the device, use the `fx shell` command:

```
fx shell
```

Now run the binary directly:

```
cowsay hi
```

```uglyprint
 _____
| hi  |
 -----
     \  ^__^
      \ (oo)\_____
        (__)\     )\/\
           ||----w |
           ||     ||
```

You may notice that you can run this program without any changes to the build
you already served to the device. Why? This example is pulled in as part of the
command line utilities in `garnet/packages/prod/BUILD.gn`. But this isn't
typically how you run a Fuchsia package; you will see that soon.

### Fortune

This sample prints a pithy observation. Let's try to run it using the extended
format for `fx shell` that accepts command arguments:

```
fx shell fortune # won't work, no package in build
```

This won't work since this package wasn't pulled into the last build. You can
confirm it isn't in the build by listing the contents of the `bin` directory:

```
fx shell ls bin
```

`cowsay` is there, but not `fortune`. Don't worry, you will add it soon.

### Hello world

This sample outputs `Hello, world!` and is written in both C++ and Rust. You may
notice something different about this sample: each language-dependent directory
also contains a `meta` subdirectory with `.cmx` files.

This type of file is known as a
[component manifest](glossary.md#component-manifest) and describes how to run
the application on Fuchsia as a [component](glossary.md#component). This is
the proper way to create a Fuchsia package.

You run a Fuchsia component by referencing its
[Fuchsia package URI](glossary.md#fuchsia_pkg-uri).

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

(For more information, see [fx workflows](development/workflows/fx.md).)

To include this package in Universe so it can be fetched on-demand, use the
`--with` flag when setting the product and board:

```
fx set ... --with //examples
fx build
```

You now have a build that includes the examples. Now you need to serve this new
build to the device. You probably already have `fx serve` running, but if not,
go ahead and start it:

```
fx serve
```

## Try the examples

Open a shell on the device to try the examples:

```
fx shell
```

### Cowsay

This should work the same as before. It is still pulled into the base image.

### Fortune

Try to run the fortune example again:

```
fortune
```

It should now return a pithy quote.

### Hello world

You can't run this sample as the other two (try to run `hello_world_cpp` or
`hello_world_rust`). To run a Fuchsia component, you need to run it's Fuchsia
package URI. Luckily, there are some built-in conveniences to help you find this.
You can use the `run` command:

```
run fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx
```

And it should output the following:

```uglyprint
Hello, World!
```

Important: You must have `fx serve` running in order to serve components from
your repository to the device. If it is not running, you may get an error from
the device (for example,
`fuchsia-pkg://fuchsia.com/hello_world_cpp#meta/hello_world_cpp.cmx: not
found`).

The `run` command can expand a string to a URI if the string only matches one
component in your product configuration:

```
run hello_world_cpp
```

If there are multiple matches, the command will list them for you to choose
from:

```
run hello
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
    locate hello_world_cpp
    ```

*   Find all runnable components in your universe.

    ```
    locate --list cmx
    ```

*   Find multiple test components.

    ```
    locate --list test
    ```
