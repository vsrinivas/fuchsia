### Packages

**Top level packages** are defined in JSON files which can be found at:

* Primary Packages Dir: [`//packages/gn`][packages-source]
* Garnet Layer Packages: [`//garnet/packages/`][garnet-packages-source].
* Peridot Layer Packages: [`//peridot/packages/`][peridot-packages-source].
* Topaz Layer Packages: [`//topaz/packages/`][topaz-packages-source].

A top level package file contains references to other top level packages it builds
on top of (`imports`) and the GN packages it includes (`packages`):
``` json
{
    "imports": [
        "packages/gn/foo_framework”,
        “packages/gn/some_extra_data”
    ],
    "packages": {
        "my_package": "//my/stuff/my_package"
    }
}
```

Packages, not to be confused with top level packages, are build targets which
define binaries.

The package name is just the label for a build target, it has no intrinsic
meaning.

> TODO(pylaligand): Document the interaction between 'package target' and 'json'
names.

```py
package("my_package") {
  deps = [
    "//path/to/some/dep"
  ],
  binaries = [{
    name = "hello_world"
  }]
}
```

Packages define what binaries they should build, as well as what targets they
depend upon.

The binaries field above specifies that this package should create a single
binary file named `hello_world_bin`, but the binaries field does not cause those
binaries to be created. Instead, one or more of the package's dependencies,
specified in `deps`, should create binaries of those names. So your package may
have `deps = ["//some/path/to:hello_world_bin"]`, and the dependency is defined
here:

```py
# file: $FUCHSIA_ROOT/some/path/to/BUILD.gn

# Executable defines a c++ binary, the label of the executable target will
# be the same as the name of the produced binary file.
executable("bin") {
  output_name = "hello_world"

  sources = [
    "my_source_file.cc",
  ]

  deps = [
    # This executable also has its own dependencies.
    "//path/to/my:first_dependency_name",
    "//path/to/another:second_dependency_name",
  ]
}
```

The above executable target will output a binary of the name `hello_world`

What the `binaries` field in a `package` target does do is let GN know to deploy
this binary onto fuchsia when you run your built image. This means that you should
be able to run your binary from inside the fuchsia shell like so:

```
$ hello_world
Hello World!
$
```




[packages-source]: https://fuchsia.googlesource.com/packages/+/master/gn
[garnet-packages-source]: https://fuchsia.googlesource.com/garnet/+/master/packages/
[peridot-packages-source]: https://fuchsia.googlesource.com/peridot/+/master/packages/
[topaz-packages-source]: https://fuchsia.googlesource.com/topaz/packages/+/master
