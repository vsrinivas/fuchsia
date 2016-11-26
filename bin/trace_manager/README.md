# Fuchsia Tracing Library

A library for recording time-series tracing data and encoding the data into the
chrome://tracing format.

## Capturing Trace Data on Device

The `trace` app takes care of starting and stopping tracing sessions and
persisting incoming trace data to disk for later retrieval with `netcp`.

`trace` supports the following command line arguments (with default values in brackets):
  - `--trace-file[=/tmp/trace.json]`: Trace data is stored here.
  - `--duration[=10] in seconds`: The trace session will last this long.
  - `--buffer-size[=2*1024*1024]`: The data pipe receiving trace data will buffer this much data.
  - `--categories[=""]`: Enable all these categories. An empty list
    enables all categories. Should be a comma-separated list of names.

Any remaining arguments are interpreted as a program to run.

An example invocation for tracing to `/tmp/trace.json` for 15 seconds,
capturing only categories `gfx` and `flutter` looks like:
```
> @ bootstrap trace --trace-file=/tmp/trace.json --duration=15 --categories=gfx,flutter launch noodles_view
```

Assuming that networking is configured correctly (see [Getting Started](../magenta/docs/getting_started.md)),
the resulting trace can then be retrieved from the device with `netcp` as in:
```
$ netcp :/tmp/trace.json trace.json
```

## Capturing Trace Data from Host

The `scripts/trace.sh` script takes care of remotely running `trace`, downloading
the trace file, and converting it to HTML for easy consumption.

This script requires the Fuchsia environment to be set up so that it can
find the necessary tools.  This also makes it possible to run `scripts/trace.sh`
by simply typing `ftrace` at the command prompt.

```
$ cd [fuchsia-root-dir]
$ source scripts/env.sh
$ fset x86-64
$ fbuild
$ ftrace --help
$ ftrace --bootstrap launch noodles_view
```

## Configuration

The tracing configuration is a JSON file consisting of a list of known
category names and descriptions.

    {
      "categories": {
        "category1": "description1",
        "category2": "description2"
        ]
      }
    }
