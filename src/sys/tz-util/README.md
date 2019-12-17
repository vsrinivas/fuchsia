# `tz-util`

Command line utility that allows you to get and set timezone ID, as well as look
up the timezone offset in minutes.

## Adding `tz-util` to your configuration

To add `tz-util` and its tests to your configuration, add the following to your
`fx set` command line:

```
--with=//src/sys/tz-util --with=//src/syz/tz-util:tests
```

Once done, `fx build` will rebuild the program.

## Development

To use a freshly-built `tz-util`, ensure that `fx serve` is running in your
Fuchsia development environment.

## Using

`tz-util` can be ran from the shell command line or by reference to its component
manifest.

### Two ways to run

Running just the binary will show usage:

```
fx shell run fuchsia-pkg://fuchsia.com/tz-util#meta/tz-util.cmx
Usage: tz-util [--help|--set_timezone_id=ID|--get_timezone_id|--get_offset_minutes]
```

You get a similar effect by referring only to the short manifest name.  The full
manifest URI can be substituted in this, and all examples below.

```
fx shell run tz-util.cmx
Usage: tz-util [--help|--set_timezone_id=ID|--get_timezone_id|--get_offset_minutes]
```

### Setting the timezone ID

Set the timezone like this:

```
fx shell run tz-util.cmx --set_timezone_id=America/Los_Angeles
```

Note that you can not set an invalid timezone ID:

```
fx shell run tz-util.cmx --set_timezone_id=Roger/Rabbit
ERROR: Unable to set ID: 1
```

The error message reports the status (with "1", meaning
[FAILED](https://fuchsia.dev/reference/fidl/fuchsia.settings/index#Error)), as
handed down from the underlying FIDL protocol
[fuchsia.settings.Intl](https://fuchsia.dev/reference/fidl/fuchsia.settings/index#Intl)

### Getting the timezone ID and offset in minutes

Get the timezone like this:

```
fx shell run tz-util.cmx --get_timezone_id
America/Los_Angeles
```

Get the timezone offset in minutes like this:

```
fx shell run tz-util.cmx --get_offset_minutes
-480
```

The result shows that an accurate clock in Los Angeles shows 8 hours less than
an accurate clock in UTC.

## Testing

`tz-util` functionality is guarded by integration tests.  It is possible to run
the `tz-util` integration tests with `fx run-test` as usual:

```
fx run-test tz-util_test
```

However, since `tz-util` is part of the base package, the former command will
also trigger a time consuming OTA.  To avoid waiting on the OTA, you can also
run the tests directly using:

```
fx shell run fuchsia-pkg://fuchsia.com/tz-util_test#meta/tz-util_test.cmx
```

However, in this case:

* Be sure to run `fx build` first, if you have changed something
* Note that some error output will be emitted only to `fx log` so when running
  the tests this way, you want to watch the `fx log` output as well.

