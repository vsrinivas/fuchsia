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
