# logger

Reviewed on: 2019-07-22

logger is the main logging service on Fuchsia. It provides the
[`fuchsia.logger.LogSink`][fidl-file] service which components use to log
messages, and the [`fuchsia.logger.Log`][fidl-file] service which
[`log_listener`][log_listener] uses to read back logs. It has a 4 MB rotating
buffer in which all logs are stored. It also reads the kernel log and merges log
messages from it into its buffer.

## Building

This project can be added to builds by including `--with //garnet/bin/logger` to
the `fx set` invocation.

## Running

logger is started by [sysmgr][sysmgr] when something needs to log, and can also
be reached by the [`log_listener`][log_listener] command line tool.

## Testing

Unit tests for logger are available in the `logger_tests` package.

Integration tests are also available in the `logger_integration_tests` package.

```
$ fx run-test logger_tests
$ fx run-test logger_integration_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, with the rest of the code living in
`src/*.rs` files. Unit tests are co-located with the code and integration tests
are located in the `tests/` directory.

[log_listener]: ../log_listener/README.md
[sysmgr]: ../sysmgr/README.md
[fidl-file]: /zircon/system/fidl/fuchsia-logger/logger.fidl
