# usb-updater

TODO: Brief overview of the component.

## Building

To add this component to your build, append
`--with src/sys/pkg/bin/usb-updater`
to the `fx set` invocation.

## Running

```
$ fx shell run fuchsia-pkg://fuchsia.com/usb-updater#meta/usb-updater.cmx
```

## Testing

Unit tests for usb-updater are available in the `usb-updater-tests`
package.

```
$ fx test usb-updater-tests
```

