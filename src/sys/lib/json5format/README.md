# json5format: Rust library and command line tool to format JSON5 with comments

Reviewed on: 2020-04-03

`json5format` is a general purpose Rust library that formats JSON5 (a.k.a., "JSON for Humans")
documents according to a configurable style, preserving comments.

The library is accompanied by a `formatjson5` command line tool.

The `json5format` library is available to fuchsia and non-fuchsia Rust programs; however,
`formatjson5` is a host-only tool, currently built and installed as a fuchsia host-side developer
tool only.

# Fuchsia use cases

The `cmc format` command uses this `json5format` library to format component manifest files
(`*.cml`) in accordance with a style specific to the CML schema, for improved readability and
consistency.

`cmc format` is also the default operation for `*.cml` files when running the fuchsia multi-schema
file formatter script `fx format-code`. This script will automatically format known file types
among the files in your most recent commit to the fuchsia source tree.

# API Documentation

Documentation for both the library and command line tool can be viewed at:

    https://fuchsia-docs.firebaseapp.com/rust/json5format/index.html

## Building

To add this project to your build, append `--with //src/sys/lib/json5format` to the `fx set`
invocation.

## Running

`formatjson5` is available at `host-tools/formatjson5` in the build output path after an `fx build`
invocation. This executable can also be launched via `fx formatjson5`.

For example, use the following command to review the command line options:

```
$ fx formatjson5 --help
```

## Testing

Make sure the tests are added to your build by adding `--with //src/sys/lib/json5format:tests` to
your `fx set` invocation.

Unit tests for the json5format library run as a fuchsia test package, defined by the
`json5format-tests` build target. To invoke the library unit tests manually, run:
invoked with the `fx run-host-tests` command:

```
$ fx test -o -v json5format-tests
```

Host-side tests for the `formatjson5` command can be manually launched by running:

```
$ fx test -o formatjson5
```