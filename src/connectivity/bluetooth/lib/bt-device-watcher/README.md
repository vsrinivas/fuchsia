# Bluetooth Library: Device Watcher

A convenience wrapper around the VFS watcher to watch for device creation and removal.

## Testing

Add the following to your Fuchsia set configuration to include the library unit tests:

`--with //src/connectivity/bluetooth/library/bt-device-watcher:tests`

To run the tests:

```
fx test bt-device-watcher-tests
```

TODO(fxbug.dev/35077): The unit tests are flaky. Fix flakes and re-enable.
