# bootfs

Tests the bootfs filesystem hosted by component manager.

## Building

To add just this component to your build, append
`--with src/sys/component_manager/tests/bootfs:tests`
to the `fx set` invocation. This is also included in the group of all component manager tests, so
prefer appending `--with src/sys/component_manager:tests` to the `fx set` invocation.

## Testing

Run the tests for bootfs using the `bootfs-tests` package.

```
$ fx test bootfs-tests
```
