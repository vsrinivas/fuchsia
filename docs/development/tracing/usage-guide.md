# Fuchsia Tracing Usage Guide

## Operational Requirements

Fuchsia tracing library and utilities require access to the `trace_manager`'s
services in the environment, which is typically set up by the
[boot sequence](/docs/concepts/framework/boot_sequence.md).

Note that capturing traces requires that the `tools` bundle be included. If your build
configuration does not include `tools` bundle by default, then you can add it manually by invoking
`fx set` like:

```{shell}
fx set PRODUCT.BOARD --with-base '//bundles:tools'
```

Note: `core` and most other products include tools ephemerally by default.

## Capturing Traces From a Development Host

Traces are captured using the `fx traceutil` host utility. To record a trace
simply run the following on your development host:

```{shell}
fx traceutil record [program arg1 ...]
```

`fx traceutil record` will:

 * Take a trace on the target using the default options.
 * Download it from the target to your development host.
 * Convert the trace into a viewable HTML file.

If a program is specified it will be run after tracing has started to not
miss any early trace events in the program.

This is a great place to start an investigation. It is also a good when you
are reporting a bug and are unsure what data is useful.

Some additional command line arguments to `fx traceutil record` include:

 * `-duration <time>`

   Sets the duration of the trace in seconds.

 * `-target <hostname or ip address>`

   Specifies which target to take a trace. Useful if you have multiple
   targets on the same network or network discovery is not working.

 * `-binary`

   Captures the trace in Fuchsia trace format on the target and performs
   the conversion to JSON on the host. Defaults to true, use
   `-binary=false` to force the conversion to happen on the target.
   This flag is planned to be removed once all workflows have been tested to be
   working with host-side conversion.

 * `-stream`

   Stream the trace output straight from the target to the host without
   saving the file on the target first.

 * `-compress`

   Compress the output stream. This is useful when saving to a small or slow
   local disk. If both `-stream` and `-compress` are provided, `-compress`
   is ignored.

 * `-decouple`

   Don't stop tracing when the traced program exits.
   This is only valid when `program` is provided.

 * `-detach`

   Don't stop the traced program when tracing finishes.
   This is only valid when `program` is provided.

 * `-spawn`

   Use `fdio_spawn` to run a legacy app.
   `-detach` will have no effect when using this option.
   This is only valid when `program` is provided.

For a complete list of command line arguments run `fx traceutil record --help`.

## Capturing Traces From a Fuchsia Target

Under the hood `traceutil` uses the `trace` utility on the Fuchsia
target to interact with the tracing manager. To record a trace run the
following in a shell on your target:

```{shell}
trace record
```

This will save your trace in /data/trace.json by default. For more information,
run `trace --help` at a Fuchsia shell.

## Viewing and Converting Between Trace Formats

There are three trace file formats that can store Fuchsia trace data.

 * FXT, or [Fuchsia Trace Format](trace-format/README.md), is a binary format
   that is a direct encoding of the original trace data that is produced by
   the various programs. To visualize this data, you can use the
   [Perfetto Trace Viewer](https://ui.perfetto.dev), which also allows you to
   [use SQL to query your trace
   data](https://www.perfetto.dev/#/trace-processor.md).
 * JSON, or
   [Chrome Trace Format](https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit).
   To visualize this data, you can use Chromium's
   [Trace-Viewer](https://github.com/catapult-project/catapult/tree/master/tracing).
 * HTML, a standalone file that includes both the viewer and trace data. To
   visualize this data, you can use a web browser such as
   [Chrome](https://google.com/chrome).

You can convert one or more files from FXT to JSON, and then to HTML by running

```{shell}
fx traceutil convert FILE ...
```

When you convert files with `fx traceutil convert`,

 * FXT files produce a corresponding JSON file and a corresponding HTML file.
 * JSON files produce a corresponding HTML file.

Note: If you record your trace with `fx traceutil record`, this conversion is
done automatically.

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
 - `measure`: array of measurement specifications, see Benchmarking

### Benchmarking

Benchmarking docs moved to a [separate doc](
/docs/development/benchmarking/trace_based_benchmarking.md).

## Configuration

The tracing configuration is a JSON file consisting of a list of known
category names and descriptions.

```json
    {
      "categories": {
        "category1": "description1",
        "category2": "description2"
      },
      "providers": {
        "provider-label": "file:///provider-to-start-automatically"
      }
    }
```
