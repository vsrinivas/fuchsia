# test-driver

This component is used a test component for `ffx component copy`. Data/files will be copied from and to this component for testing purposes.

## Building

To add this component to your build, append
`--with src/developer/ffx/plugins/component/copy/test-driver`
to the `fx set` invocation.

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

```
$ ffx component run /core/ffx-laboratory:test-driver fuchsia-pkg://fuchsia.com/test-driver#meta/test-driver.cm
```

## Testing

WIP

```
$ fx test test-driver-tests
```

