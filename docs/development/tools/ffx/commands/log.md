# Command log

`ffx log` is a log-viewing utility built into `ffx`. It has similar functionality to `fx log`, but
is built around proactively caching logs from target devices on the host machine. This feature along
with configuration options and possible side-effects are described on this page.

## Streaming logs

The basic usage for the `ffx` log viewer is:

```posix-terminal
ffx log
```

This command prints some earlier logs from your device for context, and then stream subsequent logs
as they are logged.

## Proactive logging overview

The `ffx` daemon persists in the background after an `ffx` command is run. The daemon proactively
discovers Fuchsia devices and connects to them as they become reachable .

With proactive logging, the `ffx` daemon begins reading logs from a target device in the background
as soon as it's connected. The logs are cached on the host machine, up to a configured space limit.
When the space limit is reached, logs are "rotated": the oldest logs are deleted to make room for
the newest ones.

This means that when you view logs using `ffx log`, logs are actually read from the cache on the
host machine, not directly from the target device. In general, this should not add any noticeable
delay to the log viewer, except in rare cases where the device is producing extremely large log
volumes.

### Features

Since logs are cached on the host machine, you can view logs that have been cached from a target
device that are from previous boots of the device. For example, if a device crashes, you might be
able to view the logs from the time just before the crash if they were cached in time.

You can use `ffx log dump` to view logs from a previous session. For example, to view logs from your
device's previous boot:

```posix-terminal
ffx --target <NODENAME> log dump ~1
```

`~1` identifies the session relative to the latest one you want to view, where `0` is reserved for
the currently active session for that target device (whether or not a currently active session exists). You
can view earlier boots by using `~2`, `~3`, and so on.

### Proactive log configuration

There are 3 configuration settings relevant to the proactive log cache:

- `proactive_log.max_sessions_per_target`: The maximum number of boot sessions to keep cached on the
  host. Default is 5 (that is, after 6 reboots, the logs from the oldest boot session are deleted).
- `proactive_log.max_session_size_bytes`: The maximum number of bytes to be cached for each session.
  Default is 100MB (that is, after 100MB of logs are on-disk, the oldest chunk of logs for that
  session are deleted)
- `proactive_log.max_log_size_bytes`: The maximum number of bytes to be used in a single log chunk.
  You should not generally need to change this setting. Default is 1MB.

## Symbolization

Logs are symbolized in the background as they are read from the device (before they are written to
the host log cache). However, this background processing means that misconfigurations in the
`symbolizer` host tool or with the symbol index can cause logs to be not symbolized without any
visible warning. Errors encountered when setting up the `symbolizer` tool are logged to the `ffx`
daemon log.

Users working with the Fuchsia source checkout setup do not need to perform any extra configuration;
symbolization takes place automatically as in `fx log`. Users working without the Fuchsia source
checkout setup need to configure the symbol index appropriate to their development environment.

The `ffx log` command tries to detect common misconfigurations in the symbolizer tool, but cannot
detect all of them. If your logs are not being symbolized, please
[file a bug](https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=ffx+User+Bug).

### Configuring symbolizer

There are two configuration parameters relevant to symbolization:

- `proactive_log.symbolize.enabled`: Toggles whether symbolization is attempted. Default is `true`.
- `proactive_log.symbolize.extra_args`: A raw string of additional parameters passed directly to the
  `symbolizer` host tool. This can be used to, for example, configure remote symbol servers. Default
  is `""`.