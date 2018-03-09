# Build package tools

This folder contains a set of utilities to manage build packages, i.e. files
defined under `//<layer>/packages`.


## verify_layer

This tool verifies that a given layer's `packages/` directory is properly
organized. It checks that:
- all files in the directory are JSON files;
- all files in the directory are valid according to [the schema][schema];
- all subdirectories have a file named `all` which contains all files in that
  subdirectory;
- all packages files listed as import are valid files.

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


[schema]: package_schema.json
[dot-format]: https://en.wikipedia.org/wiki/DOT_(graph_description_language)
