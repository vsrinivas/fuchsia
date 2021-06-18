# Bluetooth Library: Device Watcher

A convenience wrapper around the VFS watcher to watch for device creation and removal.

## Testing

Add the following to your Fuchsia set configuration to include the library unit tests:

`--with //src/connectivity/bluetooth/library/device-watcher:tests`

To run the tests:

```
fx test device-watcher-tests
```

TODO(fxbug.dev/35077): The unit tests are flaky. Fix flakes and re-enable.
