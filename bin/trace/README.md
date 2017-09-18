# trace - collects and converts trace data

trace enables tracing of an application. It exposes the following
command line interface (invoke trace with --help to get an overview):

```{shell}
trace [options] command [command-specific options]
  --help: Produce this help message

  dump-provider - dumps provider with specified id
  list-categories - list all known categories
  list-providers - list all registered providers
  record - starts tracing and records data
    --[command args]: Run program before starting trace. The program is terminated when tracing ends unless --detach is specified
    --append-args=[""]: Additional args for the app being traced, appended to those from the spec file, if any
    --buffer-size=[4]: Maximum size of trace buffer for each provider in megabytes
    --categories=[""]: Categories that should be enabled for tracing
    --decouple=[false]: Don't stop tracing when the traced program exits
    --detach=[false]: Don't stop the traced program when tracing finished
    --duration=[10s]: Trace will be active for this long after the session has been started
    --output-file=[/data/trace.json]: Trace data is stored in this file
    --spec-file=[none]: Tracing specification file
    --upload-bot=[none]: Buildbot builder name
    --upload-master=[none]: Name of the buildbot master
    --upload-point-id=[none]: Integer identifier of the sample
    --upload-server-url=[none]: Url of the Catapult dashboard server
```
