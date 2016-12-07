# Fuchsia Tracing Library

A library for recording time-series tracing data and encoding the data into the
chrome://tracing format.

## Capturing Trace Data on Device

The `trace` app takes care of starting and stopping tracing sessions and
persisting incoming trace data to disk for later retrieval with `netcp`.

```{shell}
trace [options] command [command-specific options]
  --help: Produce this help message

  list-categories - list all known categories
  list-providers - list all registered providers
  record - starts tracing and records data
    --categories=[""]: Categories that should be enabled for tracing
    --detach: Don't stop the traced program when tracing finished
    --decouple: Don't stop tracing when the traced program exits
    --duration=[10s]: Trace will be active for this long
    --output-file=[/tmp/trace.json]: Trace data is stored in this file
	[command args]: Run program before starting trace. The program is terminated when tracing ends unless --detach is specified
```
Any remaining arguments are interpreted as a program to run.

An example invocation for tracing to `/tmp/trace.json` for 15 seconds,
capturing only categories `gfx` and `flutter` looks like:
```
> @ bootstrap trace record --trace-file=/tmp/trace.json --duration=15 --categories=gfx,flutter launch noodles_view
```

Assuming that networking is configured correctly (see [Getting Started](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md)),
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
      },
      "providers": {
        "provider-label": "file:///provider-to-start-automatically"
      }
    }
