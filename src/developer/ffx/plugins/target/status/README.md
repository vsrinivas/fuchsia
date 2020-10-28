# Target status

This is a subcommand of the `ffx target` command. It provides options to:

- get status of the target device
- get status of the target device in a machine readable format

The code runs on the host and communicates to software running on the target
Fuchsia device via FIDL through an overnet connection.

## Build

To build the target status command, build ffx which statically links the plugin
lib. E.g.

```
$ fx build src/developer/ffx
```

## Test

Run tests with

```
$ fx test ffx_target_status_lib_test
```
