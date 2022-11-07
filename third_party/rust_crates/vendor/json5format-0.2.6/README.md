# json5format

**`json5format` is a general purpose Rust library that formats [JSON5](https://json5.org) (a.k.a., "JSON for Humans"), preserving contextual line and block comments.**

[![crates.io](https://img.shields.io/crates/v/json5format.svg)](https://crates.io/crates/json5format)
[![license](https://img.shields.io/badge/license-BSD3.0-blue.svg)](https://github.com/google/json5format/LICENSE)
[![docs.rs](https://docs.rs/com/badge.svg)](https://docs.rs/crate/json5format/)
![json5format](https://github.com/google/json5format/workflows/json5format/badge.svg)

## `json5format` Rust library

The [`json5format` library](https://crates.io/crates/json5format) includes APIs to customize the document format, with style options configurable both globally (affecting the entire document) as well as tailoring specific subsets of a target JSON5 schema. (See the [Rust package documentation](https://docs.rs/json5format/0.1.0/json5format) for more details and examples.) As of version 0.2.0, public APIs allow limited support for accessing the information inside a parsed document, and for injecting or modifying comments.
## `formatjson5` command line tool

The `json5format` package also bundles an [example command line tool, `formatjson5`,](https://github.com/google/json5format/blob/master/examples/formatjson5.rs) that formats JSON5 documents using a basic style with some customizations available through command line options:

```
$ cargo build --example formatjson5
$ ./target/debug/examples/formatjson5 --help

formatjson5 [FLAGS] [OPTIONS] [files]...

FLAGS:
-h, --help                  Prints help information
-n, --no_trailing_commas    Suppress trailing commas (otherwise added by default)
-o, --one_element_lines     Objects or arrays with a single child should collapse to a
                            single line; no trailing comma
-r, --replace               Replace (overwrite) the input file with the formatted result
-s, --sort_arrays           Sort arrays of primitive values (string, number, boolean, or
                            null) lexicographically
-V, --version               Prints version information

OPTIONS:
-i, --indent <indent>    Indent by the given number of spaces [default: 4]

ARGS:
<files>...    Files to format (use "-" for stdin)
```

NOTE: This is not an officially supported Google product.
