# formatjson5: Command line tool to format JSON5 with comments

Reviewed on: 2020-04-24

`formatjson5` is a command line tool, currently built and installed as a fuchsia developer tool
(host-side only).

The binary is Rust-based, built using the [`json5format`](https://crates.io/crates/json5format)
library. (`json5format` was originally developed by the Fuchsia team, within the Fuchsia source
tree, but was later published as an independent Rust library, under [crates.io](https://crates.io),
and is now imported into Fuchsia's `third_party/rust_crates`.)

# Fuchsia use cases

`json5format` is the default formatter for some file extensions supported by the fuchsia multi-
schema file formatter script, `fx format-code`. This script will automatically format known file
types among the files in your most recent commit to the fuchsia source tree.

# Documentation

Run `fx formatjson5 --help` for command line usage documentation.

## Building

To add this project to your build, append `--with //build/tools/formatjson5` to the `fx set`
invocation.

## Running

`formatjson5` is available at `host-tools/formatjson5` in the build output path after an `fx build`
invocation. This executable can also be launched via `fx formatjson5`.

For example, use the following command to review the command line options:

```
$ fx formatjson5 --help
```

## Testing

Make sure the tests are added to your build by adding `--with //build/packages:formatjson5` to
your `fx set` invocation.

Host-side tests for the `formatjson5` command can be manually launched by running:

```
$ fx test -o formatjson5
```