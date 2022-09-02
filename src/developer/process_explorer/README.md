# process_explorer

A component that gets all the processes on a running Fuchsia device,
and for each of them it retrieves information about all the handles
in the process. The data is then packaged as a string in JSON format
and sent over a socket.

## Building

To add this component to your build, append
`--with-base src/developer/process_explorer`
to the `fx set` invocation.

## Running

Use `ffx component start` to start this component for development purposes:

```
$ ffx component start /core/process_explorer
```

## Testing

Unit tests for process_explorer are available in the `process_explorer_tests`
package.

```
$ fx test process_explorer_tests
```

