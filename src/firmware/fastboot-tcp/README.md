# fastboot-tcp

A component that implements fastboot over tcp protocol.

The component is under construction and not ready for use.

## Building

To add this component to your build, append
`--with-base src/firmware/fastboot-tcp`
to the `fx set` invocation.

## Running

Use `ffx component run` to launch this component into a restricted realm
for development purposes:

```
$ ffx component run fuchsia-pkg://fuchsia.com/fastboot-tcp#meta/fastboot-tcp.cm
```

## Testing

Unit tests for fastboot-tcp are available in the `fastboot-tcp-tests`
package.

```
$ fx test fastboot-tcp-tests
```

