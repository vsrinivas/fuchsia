# Target show

This is a subcommand of the `ffx target` command. It provides options to:

- get information from the target device
- get the information of the target device in a machine readable format

The code runs on the host and communicates to software running on the target
Fuchsia device via FIDL through an overnet connection.

## Build

To build the target show command, build ffx which statically links the plugin
lib. E.g.

```
$ fx build src/developer/ffx
```

## Test

Run tests with

```
$ fx test ffx_target_show_lib_test
```
