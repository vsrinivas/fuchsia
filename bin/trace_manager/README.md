# Fuchsia Tracing Library

A library for recording time-series tracing data and encoding the data into the
chrome://tracing format.

[TOC]

## Operational Requirements

Fuchsia tracing library and utilities require access to the `trace_manager`'s
services in the environment, which is typically set up by the
[boot sequence](https://fuchsia.googlesource.com/docs/+/master/boot_sequence.md).

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
    --spec-file=[none]: Tracing specification file
	[command args]: Run program before starting trace. The program is terminated when tracing ends unless --detach is specified
```
Any remaining arguments are interpreted as a program to run.

An example invocation for tracing to `/tmp/trace.json` for 15 seconds,
capturing only categories `gfx` and `flutter` looks like:
```
trace record --trace-file=/tmp/trace.json --duration=15 --categories=gfx,flutter launch noodles_view
```

Assuming that networking is configured correctly (see [Getting Started](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md)),
the resulting trace can then be retrieved from the device with `netcp` as in:
```
$ netcp :/tmp/trace.json trace.json
```

### Tracing specification file

Tracing specification file is a JSON file that can be passed to `trace record`
in order to configure parameters of tracing. For those parameters that can be
passed both on the command line and set in the specification file, the command
line value overrides the one from the file.

The file supports the following top level-parameters:

 - `"app"`: string, url of the application to be run
 - `"args"`: array of strings, startup arguments to be passed to the application
 - `"categories"`: array of strings, tracing categories to be enabled
 - `"duration"`: integer, duration of tracing in seconds
 - `"measure"`: array of measurement specifications, see below

### Benchmarking

Performance benchmarks can be defined in the tracing specification file as
measurements to be performed on the captured trace events.

Example:

```
{
  "app": "ledger_benchmark_put",
  "args": ["--entry-count=10", "--value-size=100"],
  "categories": ["benchmark", "ledger"],
  "measure": [
    {
      "type": "duration",
      "event_name": "put",
      "event_category": "benchmark"
    },
    {
      "type": "time_between",
      "first_event_name": "initialized",
      "first_event_category": "ledger",
      "second_event_name": "commit_download",
      "second_event_category": "ledger",
      "second_event_anchor": "begin"
    }
  ]
}
```

A `"duration"` measurement targets a single trace event and computes the
duration of its occurences. The target trace event can be recorded as a
"duration" or as an "async" event. Takes arguments: `"event_name"`,
`"event_category"`.

A `"time_between"` measurement targets two trace events with the specified
anchors (either the beginning or the end of the events) and computes the time
between the consecutive occurences of the two. The target events can be
"duration", "async" or "instant" (in which case the anchor doesn't matter).
Takes arguments: `"first_event_name"`, `"first_event_category"`,
`"first_event_anchor"`, `"second_event_name"`, `"second_event_category"`,
`"second_event_anchor"`.

In the example above the `"time_between"` measurement captures the time between
the instant event "initialized" and the beginning of the duration event
"commit_download".

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
