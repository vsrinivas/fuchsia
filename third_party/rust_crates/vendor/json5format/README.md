# json5format

**`json5format` is a general purpose Rust library that formats JSON5 (a.k.a., "JSON for Humans"),
preserving contextual line and block comments.**

The `json5format` library includes APIs to customize the document format, with style options
configurable both globally (affecting the entire document) as well as tailoring specific subsets of
a target JSON5 schema. (See the Rust package documentation for more details and examples.)

The `json5format` package also bundles a sample command line tool that formats JSON5 documents
using a basic style with some customizations available through command line options. (After
`cargo build`, run `./target/debug/examples/formatjson5 --help` for more details.)

NOTE: This is not an officially supported Google product.
