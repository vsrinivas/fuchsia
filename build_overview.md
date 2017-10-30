# The build system


### Overview

The Fuchsia build system aims at building complete boot images for various
devices. To do so, it uses [GN][gn-main], a meta-build system that generates
build files consumed by [Ninja][ninja-main] to execute the actual build.

The contents of the generated image are controlled by a set of packages defining
what should go into the GN build and what should be packaged into the image.


### Packages

You can configure what gets built using the [packaging system][packages-source].

### Build targets

Build targets are defined in `BUILD.gn` files scattered all over the source
tree. These files use a Python-like syntax to declare buildable objects:
``` py
import("//build/some/template.gni")

my_template("foo") {
  name = "foo"
  extra_options = "//my/foo/options"
  deps = [
    "//some/random/framework",
    "//some/other/random/framework",
  ]
}
```
Available commands (invoked using gn cli tool) and constructs (built-in target
declaration types) are defined in the [GN reference][gn-reference]. There are
also a handful of custom templates in `.gni` files in the
[`//build` project][build-project].

These custom templates mostly define custom target declaration types, such as
the [package declaration type][packages-source].

> TODO(pylaligand): list available templates

### Executing a build

##### A

The first step is to build the Zircon kernel which uses its own build system:
```
$ scripts/build-zircon.sh
```

##### B

Then configure the content of the generated image by choosing the packages to
incorporate:
```
# fuchsia_base is typically "default".
# my_stuff is a possibly-empty list of extra packages to include.

$ packages/gn/gen.py --packages packages/gn/fuchsia_base,packages/gn/my_stuff
```
This will create an `out/debug-<arch>` directory containing Ninja files to run
the build.


##### C

The final step is to run the actual build with Ninja:
```
$ buildtools/ninja -C out/debug-x86-64 -j 64
```

### Rebuilding

#### After modifying non-Zircon files

In order to rebuild the tree after modifying some sources, just rerun step
**C**. This holds true even if you modify `BUILD.gn` files as GN adds Ninja
targets to update Ninja targets if build files are changed! The same holds true
for package files used to configure the build.

#### After modifying Zircon files

You will want to rerun **A** and **C**.

#### After syncing sources

You’ll most likely need to run **A** once if anything in the Zircon tree was
changed. After that, run **C** again.


### Tips and tricks

#### Visualizing the hierarchy of build packages

```
$ scripts/visualize_module_tree.py > tree.dot
$ dot -Tpng tree.dot -o tree.png
```

#### Inspecting the content of a GN target

```
$ buildtools/gn desc out/debug-x86-64 //path/to/my:target
```

#### Finding references to a GN target

```
$ buildtools/gn refs out/debug-x86-64 //path/to/my:target
```

#### Referencing targets for the build host

Various host tools (some used in the build itself) need to be built along with
the final image.

To reference a build target for the host toolchain from a module file:
```
//path/to/target(//build/toolchain:host_x64)
```
To reference a build target for the host toolchain from within a `BUILD.gn`
file:
```
//path/to/target($host_toolchain)
```

#### Building only a specific target

If a target is defined in a GN build file as `//foo/bar/blah:dash`, that target
(and its dependencies) can be built with:
```
$ buildtools/ninja -C out/debug-x86-64 -j64 foo/bar/blah:dash
```
Note that this only works for targets in the default toolchain.

#### Exploring Ninja targets

GN extensively documents which Ninja targets it generates. The documentation is
accessible with:
```
$ buildtools/gn help ninja_rules
```

You can also browse the set of Ninja targets currently defined in your output
directory with:
```
$ buildtools/ninja -C out/debug-x86-64 -t browse
```
Note that the presence of a Ninja target does not mean it will be built - for
that it needs to depend on the “default” target.

#### Understanding why Ninja does what it does

Add `-d explain` to your Ninja command to have it explain every step of its
execution.

#### Debugging build timing issues

When running a build, Ninja keeps logs that can be used to generate
visualizations of the build process:

1. Delete your output directory - this is to ensure the logs represent only the
   build iteration you’re about to run;
1. Run a build as you would normally do;
1. Get <https://github.com/nico/ninjatracing>;
1. Run `ninjatracing <output directory>`;
1. Load the resulting json file in Chrome in `about:tracing`.


### Troubleshooting

#### My GN target is not being built!

Make sure it rolls up to a label defined in a module file, otherwise the build
system will ignore it.

#### GN complains about a missing `sysroot`.

You likely forgot to run **A** before running **B**.

> TODO(pylaligand): command showing path to default target


### Internal GN setup

> TODO(pylaligand): .gn, default target, mkbootfs, GN labels insertion


[gn-main]: https://chromium.googlesource.com/chromium/src/tools/gn/+/HEAD/README.md
[ninja-main]: https://ninja-build.org/
[gn-reference]: https://chromium.googlesource.com/chromium/src/tools/gn/+/HEAD/docs/reference.md
[build-project]: https://fuchsia.googlesource.com/build/+/master/
[packages-source]: https://fuchsia.googlesource.com/docs/+/master/build_packages.md
