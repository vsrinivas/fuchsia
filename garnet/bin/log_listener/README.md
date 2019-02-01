# `log_listener`

For general usage of `log_listener`, please see `log_listener --help`.

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
