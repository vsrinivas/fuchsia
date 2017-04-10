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

An example invocation for tracing for 15 seconds,
capturing only categories `gfx` and `flutter` looks like:
```
trace record --duration=15 --categories=gfx,flutter launch noodles_view
```

The default output file is `/tmp/trace.json`.
Assuming that networking is configured correctly (see [Getting Started](https://fuchsia.googlesource.com/magenta/+/master/docs/getting_started.md)),
the resulting trace can then be retrieved from the device with `netcp` as in:
```
$ netcp :/tmp/trace.json trace.json
```

### Visualizing trace files

You can use [Chromium](https://www.chromium.org/Home)'s (or
[Google Chrome](https://www.google.com/chrome/)'s) [Trace Event Profiling
Tool](https://www.chromium.org/developers/how-tos/trace-event-profiling-tool) to
visualize `trace.json` files:

1.  Navigate to `chrome://tracing/`
1.  Click the **Load** button
1.  Select the `trace.json` file you copied off the fuchsia device

### Tracing specification file

Tracing specification file is a JSON file that can be passed to `trace record`
in order to configure parameters of tracing. For those parameters that can be
passed both on the command line and set in the specification file, the command
line value overrides the one from the file.

The file supports the following top level-parameters:

 - `app`: string, url of the application to be run
 - `args`: array of strings, startup arguments to be passed to the application
 - `categories`: array of strings, tracing categories to be enabled
 - `duration`: integer, duration of tracing in seconds
 - `measure`: array of measurement specifications, see below

### Benchmarking

Performance benchmarks can be defined in the tracing specification file as
measurements to be performed on the captured trace events.

Example:

```
{
  "app": "ledger_benchmark_put",
  "args": ["--entry-count=100", "--value-size=1000"],
  "categories": ["benchmark", "ledger"],
  "measure": [
    {
      "type": "duration",
      "split_samples_at": [1, 50],
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

A `duration` measurement targets a single trace event and computes the
duration of its occurences. The target trace event can be recorded as a
duration or as an async event. Takes arguments: `event_name`,
`event_category`.

A `time_between` measurement targets two trace events with the specified
anchors (either the beginning or the end of the events) and computes the time
between the consecutive occurences of the two. The target events can be
"duration", "async" or "instant" (in which case the anchor doesn't matter).
Takes arguments: `first_event_name`, `first_event_category`,
`first_event_anchor`, `second_event_name`, `second_event_category`,
`second_event_anchor`.

In the example above the `time_between` measurement captures the time between
the instant event "initialized" and the beginning of the duration event
"commit_download".

Both `duration` and `time_between` measurements can optionally group the
recorded samples into consecutive ranges, splitting the samples at the given
instances of the recorded events and reporting the results of each group
separately. In order to achieve that, pass a strictly increasing list of
zero-based numbers denoting the occurences at which samples must be split as
`split_samples_at`.

For example, if a measurement specifies `"split_samples_at": [1, 50],`, the
results will be reported in three groups: sample 0, samples 1 - 49, and samples
50 to N, where N is the last samples.

#### Catapult dashboard upload

Results of the benchmarking run can be uploaded to an instance of the
[Catapult](https://github.com/catapult-project/catapult) dashboard. For that,
the following additional parameters must be passed to `trace record`:

 - `--upload-server-url` - the url of the Catapult dashboard server
 - `--upload-master` - name of the buildbot master
 - `--upload-bot` - name of the builder
 - `--upload-point-id` - integer sequence number for the new samples

In order to experiment against a local instance of the dashboard, follow these
[instructions](docs/catapult.md).

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
