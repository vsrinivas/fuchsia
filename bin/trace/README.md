# trace - collects and converts trace data

trace enables tracing of an application. It exposes the following
command line interface (invoke trace with --help to get an overview):

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

## Tracing specification file

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

## Benchmarking

Tracing specification supports defining measurements to be performed on captured
trace events, allowing us to define performance benchmarks in the spec file.

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
