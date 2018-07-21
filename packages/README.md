# Build package and products tools

This folder contains a set of utilities to manage products and build
packages, i.e. files defined under `//<layer>/{packages,products}`.


## verify_layer

This tool verifies that a given layer's `packages/` and `products/`
directories are properly organized. It checks that:

- all files in the directories are JSON files;
- all package files are valid according to [the package schema][package-schema];
- all product files are valid according to [the product schema][package-schema];
- all package subdirectories (except a few canonical ones for which it does not make
  sense) have a file named `all` which contains all files in that subdirectory;
- all packages files listed as import are valid files;
- the root directories contain a set of canonical files.

The tool relies on a JSON validator commonly built as part of the Fuchsia build.
The validator can be found at:
```sh
out/<build_type>/<host_toolchain>/json_validator
```


## visualize_hierarchy

This tool generates a visualization of the package hierarchy for a given package
file. The resulting graph file uses the [DOT format][dot-format].

In order to generate an image file from the graph file, use the following
command:
```sh
dot -Tpng <graph.dot> -o graph.png
```


[package-schema]: package_schema.json
[product-schema]: product_schema.json
[dot-format]: https://en.wikipedia.org/wiki/DOT_(graph_description_language)
