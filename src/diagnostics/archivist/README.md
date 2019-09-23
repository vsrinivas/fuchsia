# Diagnostics Archivist

Reviewed on: 2019-08-30

logger is the main logging service on Fuchsia. It provides the
[`fuchsia.logger.LogSink`][fidl-file] service which components use to log
messages, and the [`fuchsia.logger.Log`][fidl-file] service which
[`log_listener`][log_listener] uses to read back logs. It has a 4 MB rotating
buffer in which all logs are stored. It also reads the kernel log and merges log
messages from it into its buffer.

## Building

This project is included in the `core` build product.

## Running

The diagnostics archivist is started on-demand by clients connecting to the `LogSink` protocol. In
practice this means an instance is usually running already.

## Testing

Unit tests are available in the `archivist_tests` package.

Integration tests for system logging are available in the `logger_integration_tests` package.

```
$ fx run-test archivist_tests
$ fx run-test logger_integration_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, with the rest of the code living in
`src/*.rs` files. Unit tests are co-located with the code and integration tests
are located in the `tests/` directory.

[log_listener]: ../../../garnet/bin/log_listener/README.md
[sysmgr]: ../../sys/sysmgr/README.md
[fidl-file]: ../../../zircon/system/fidl/fuchsia-logger/logger.fidl
