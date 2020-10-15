#  Build inputs

This directory contains targets which represent the various GN arguments used to
alter the build graph.
Instead of directly using a given GN argument in a build file, add a dependency
on the matching target defined in this directory.


## Structure

The `board`, `product`, and `dev` subdirectories host targets for GN arguments
declared respectively in `//build/board.gni`, `//build/product.gni`, and
`//build/dev.gni`.
In addition, the build file in the root directory contains targets which
aggregate targets from subdirectories.


## Benefits

### Traceability

This is most obvious when using tools such as `gn path` which will show the
targets in the output paths instead of jumping from the target that needs the
GN argument straight to the labels listed in that argument.

Compare:
```
//i/am:foo --[private]-->
//this/is:bar
```
to:
```
//i/am:foo --[private]-->
//build/input/board:bootfs_deps --[public]-->
//this/is:bar
```

The latter output makes it very clear how `//this/is:bar` is introduced in the
build graph.

### Indirection

The targets defined in the root directory may combine targets from multiple
subdirectories.
This level of indirection makes it easier to introduce or remove GN arguments
without having to modifying all call sites.

For example, an aggregation target will expose all dependencies for the main
zbi; currently these dependencies are sourced from GN arguments from the board
and the product files, but in the future we may add an argument to let
developers manually add to the zbi.
Thanks to the aggregation target, this change would be invisible to the targets
actually declaring the zbi itself.
