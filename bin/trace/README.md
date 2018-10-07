# trace - collects and converts trace data

trace enables tracing of an application. It exposes the following
command line interface (invoke trace with --help to get an overview):

```{shell}
trace [options] command [command-specific options]
  --help: Produce this help message

  list-categories - list all known categories
  record - starts tracing and records data
    --[command args]: Run program before starting trace.
        The program is terminated when tracing ends unless --detach is specified
    --append-args=[""]: Additional args for the app being traced, appended to
        those from the spec file, if any
    --buffer-size=[4]: Maximum size of trace buffer for each provider in
        megabytes
    --buffering-mode=[oneshot]: Specify buffering mode as one of oneshot,
        circular, or streaming
    --categories=[""]: Categories that should be enabled for tracing
    --decouple=[false]: Don't stop tracing when the traced program exits
    --detach=[false]: Don't stop the traced program when tracing finished
    --duration=[10s]: Trace will be active for this long after the session has
        been started
    --output-file=[/data/trace.json]: Trace data is stored in this file
        The output file may be "tcp:IP-ADDRESS:PORT" in which case a stream
        socket is connected to that address and trace data is streamed directly
        to it instead of saving the output locally. Streaming via TCP is
        generally only done when invoked by traceutil.
    --spec-file=[none]: Tracing specification file
```
