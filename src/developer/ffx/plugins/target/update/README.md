# Update target device

This is a subcommand of the `ffx target` command. It provides options to:

- check whether the target device is up to date
- request (and monitor) an update to the device
- [wip] force the device to "update" to a specific system image (regardless of
  whether that system image is older than the current image)

The code runs on the host and communicates to software running on the target
Fuchsia device via FIDL through an overnet connection.

## Build

To build the target update command, build ffx which statically links the update
lib. E.g.

```
$ fx build src/developer/ffx
```

## Test

Run tests with

```
$ fx test ffx_update_lib_test
```
