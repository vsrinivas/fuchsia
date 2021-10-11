# `log_listener`

Reviewed on: 2020-08-27

`log_listener` is a command line tool for retrieving log messages from
[`fuchsia.logger.Log`][logger] and either writing them to disk or printing them.

## Building

This tool is included in the `core` build product.

## Running

`log_listener` is typically invoked via the `fx log` command:

```
$ fx log --help
```

It can also be run directly on a Fuchsia shell:

```
$ fx shell log_listener --help
```

## Testing

Unit tests for `log_listener` are available in the `log_listener_tests`
package.

```
$ fx test log_listener_tests
```

## Source layout

The implementation is located in `src/main.rs`. Unit tests are co-located with
the code.

## Persisting logs to disk

An instance of  `log_listener` is always started to persist logs to disk, but by
default is set to use 0 bytes of disk space. To change how much disk space
`log_listener` is allowed to use to persist logs, append this argument to your
`fx set` invocation:

```
--args 'max_log_disk_usage="64000"'
```

This will cause logs to be placed in `/data/logs.all_logs`. Once this file
reaches half of the allowed maximum, it is renamed to `/data/logs.all_logs.old`
and future data is written to a newly-created `/data/logs.all_logs`.

[logger]: ../../../src/diagnostics/archivist/README.md
