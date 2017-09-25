# Fuchsia Tracing Usage Guide

## Operational Requirements

Fuchsia tracing library and utilities require access to the `trace_manager`'s
services in the environment, which is typically set up by the
[boot sequence](https://fuchsia.googlesource.com/docs/+/master/boot_sequence.md).

## Capturing Traces From a Development Host

Traces are captured using the `traceutil` host utility.  To record a trace
simply run the following on your development host:

```{shell}
traceutil record
```

This will:
 * Take a trace on the target using the default option.
 * Download it from the target to your development host.
 * Convert the trace into a viewable HTML file.

This is a great place to start an investigation.  It is also a good when you
are reporting a bug and are unsure what data is useful.

Some additional command line arguments to `traceutil record` include:
 * `--duration <time>`

   Sets the duration of the trace in seconds.

 * `--target <hostname or ip address>`

   Specifies one which target to take a trace.  Useful if you have multiple
   targets on the same network or network discovery is not working.

For a complete list of command line arguments run `traceutil --help`.

## Capturing Traces From a Fuchsia Target

Under the hood `traceutil` uses the `trace` utility on the Fuchsia
target to interact with the tracing manager.  To record a trace run the
following in a shell on your target:

```{shell}
trace record
```

This will save your trace in /data/trace.json by default.  For more information
on, run `trace --help` at a Fuchsia shell.

## Converting a JSON Trace to a Viewable HTML Trace.

The Fuchsia tracing system uses Chromium's
[Trace-Viewer](https://github.com/catapult-project/catapult/tree/master/tracing).
The easiest way to view a JSON trace is to embed it into an HTML file with
Trace-Viewer.  To convert one or more JSON files run:

```{shell}
traceutil convert FILE ...
```

The HTML files written are standalone and can be opened in the
[Chrome](https://google.com/chrome) web browser.

## Advanced Tracing

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
measurements to be performed on the captured trace events.  A example can be
found in [src/benchmark_example](../src/benchmark_example/).

[benchmark_example.json](../src/benchmark_example/benchmark_example.json) can
be broken up into three sections.

The first section defines the top level parameters listed above:
```{json}
{
  "app": "benchmark_example",
  "args": [],
  "categories": ["benchmark"],
  "measure": [
    ...
  ]
}
```

The second section defines a `duration` measurement:
```{json}
    {
      "type": "duration",
      "split_samples_at": [1, 50],
      "event_name": "example",
      "event_category": "benchmark"
    },
```
A `duration` measurement targets a single trace event and computes the
duration of its occurences. The target trace event can be recorded as a
duration or as an async event. Takes arguments: `event_name`,
`event_category`.

The third section defines a `time_between` measurement:
```{json}
    {
      "type": "time_between",
      "first_event_name": "task_end",
      "first_event_category": "benchmark",
      "second_event_name": "task_start",
      "second_event_category": "benchmark"
    }
```

A `time_between` measurement targets two trace events with the specified
anchors (either the beginning or the end of the events) and computes the time
between the consecutive occurrences of the two. The target events can be
"duration", "async" or "instant" (in which case the anchor doesn't matter).
Takes arguments: `first_event_name`, `first_event_category`,
`first_event_anchor`, `second_event_name`, `second_event_category`,
`second_event_anchor`.

In the example above the `time_between` measurement captures the time between
the two instant events and measures the time between the end of one task and
the beginning of another.

Both `duration` and `time_between` measurements can optionally group the
recorded samples into consecutive ranges, splitting the samples at the given
instances of the recorded events and reporting the results of each group
separately. In order to achieve that, pass a strictly increasing list of
zero-based numbers denoting the occurrences at which samples must be split as
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
