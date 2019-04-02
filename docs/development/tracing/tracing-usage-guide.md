# Fuchsia Tracing Usage Guide

## Operational Requirements

Fuchsia tracing library and utilities require access to the `trace_manager`'s
services in the environment, which is typically set up by the
[boot sequence](/docs/the-book/boot_sequence.md).

Note that capturing traces requires that the `devtools` package be included.  If your build
configuration does not include `devtools` by default, then you can add it manually by invoking
`fx set` like:

```{shell}
fx set $ARCH --monolith='garnet/packages/products/devtools,$DEFAULT_PACKAGES'
```

where `ARCH` is the target architecture, and `DEFAULT_PACKAGES` is the default package list for the
selected layer.  So if you were targeting `x64` with `topaz` selected, you would run:

```{shell}
fx set x64 --monolith='garnet/packages/products/devtools,topaz/packages/default'
```

Note: If you need to enable flutter profiling on `--release` builds, include `flutter_profile=true`
gn argument to `fx set`. Example:
```{shell}
fx set x64 --release --monolith='garnet/packages/products/devtools,topaz/packages/default' --args=flutter_profile=true
```

## Capturing Traces From a Development Host

Traces are captured using the `fx traceutil` host utility.  To record a trace
simply run the following on your development host:

```{shell}
fx traceutil record
```

This will:
 * Take a trace on the target using the default option.
 * Download it from the target to your development host.
 * Convert the trace into a viewable HTML file.

This is a great place to start an investigation.  It is also a good when you
are reporting a bug and are unsure what data is useful.

Some additional command line arguments to `fx traceutil record` include:
 * `-duration <time>`

   Sets the duration of the trace in seconds.

 * `-target <hostname or ip address>`

   Specifies which target to take a trace.  Useful if you have multiple
   targets on the same network or network discovery is not working.

 * `-stream`

   Stream the trace output straight from the target to the host without
   saving the file on the target first.

 * `-compress`

   Compress the output stream. This is useful when saving to a small or slow
   local disk. If both `-stream` and `-compress` are provided, `-compress`
   is ignored.

For a complete list of command line arguments run `fx traceutil record --help`.

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
fx traceutil convert FILE ...
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
 - `measure`: array of measurement specifications, see Benchmarking

### Benchmarking

Benchmarking docs moved to a [separate doc](
http://fuchsia.googlesource.com/fuchsia/+/master/docs/development/benchmarking/trace_based_benchmarking.md).

## Configuration

The tracing configuration is a JSON file consisting of a list of known
category names and descriptions.

    {
      "categories": {
        "category1": "description1",
        "category2": "description2"
      },
      "providers": {
        "provider-label": "file:///provider-to-start-automatically"
      }
    }
