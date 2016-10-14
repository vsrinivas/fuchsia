# Fuchsia Tracing Library

A library for recording time-series tracing data and encoding the data into the
chrome://tracing format.

## Capturing Trace Data

The `tracer` app takes care of starting and stopping tracing sessions and
persisting incoming trace data to disk for later retrieval with `netcp`.

`tracer` supports the following command line arguments (with default values in brackets):
  - `--trace-file[=/tmp/trace.json]`: Trace data is stored here.
  - `--duration[=10] in seconds`: The trace session will last this long.
  - `--buffer-size[=2*1024*1024]`: The data pipe receiving trace data will buffer this much data.
  - `--categories[=""]`: Enable all these categories. An empty list
    enables all categories. Should be a comma-separated list of names.

An example invocation for tracing to `/tmp/trace.json` for 15 seconds,
capturing only categories `gfx` and `flutter` looks like:
```
$ mojo:tracer --trace-file=/tmp/trace.json --duration=15 --categories=gfx,flutter
```

Assuming that networking is configured correctly (see [Getting Started](../magenta/docs/getting_started.md)),
the resulting trace can then be retrieved from the device with `netcp` as in:
```
$ netcp :/tmp/trace.json trace.json
```
