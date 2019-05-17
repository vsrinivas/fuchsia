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
    --provider-buffer-size=[provider-name:buffer-size]:
        Specify the buffer size that "provider-name" will use.
        May be specified multiple times, once per provider.
    --buffering-mode=[oneshot]: Specify buffering mode as one of oneshot,
        circular, or streaming
    --categories=[""]: Categories that should be enabled for tracing
    --decouple=[false]: Don't stop tracing when the traced program exits
    --detach=[false]: Don't stop the traced program when tracing finished
    --spawn=[false]: Use fdio_spawn to run a legacy app.
        Detach will have no effect when using this option.
    --environment-name=[none]: Create a nested environment with the given
        name and run the app being traced under it.
    --return-child-result=[true]: Pass on the child's return code.
    --duration=[10]: Trace will be active for this many seconds after the
        session has been started. The provided value must be integral.
    --output-file=[/data/trace.json]: Trace data is stored in this file
        The output file may be "tcp:IP-ADDRESS:PORT" in which case a stream
        socket is connected to that address and trace data is streamed directly
        to it instead of saving the output locally. Streaming via TCP is
        generally only done when invoked by traceutil.
    --compress=[false]: Compress the output stream. Compressing a network
        output stream is not supported, if both --output-file=tcp:... and
        --compress are provided, --compress is ignored.
    --spec-file=[none]: Tracing specification file
```

Options provided on the command line override options found in the
`--spec-file` spec file.
