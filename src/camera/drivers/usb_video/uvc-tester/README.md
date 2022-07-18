# uvc-tester

A manual test for the usb_video camera driver.

## Building

To add this component to your build, append
`--with-base src/camera/drivers/usb_video/uvc-tester`
to the `fx set` invocation.

## Running

This component requires a usb camera plugged into the fuchsia device.

Use `ffx driver run-tool` to launch this component into a restricted realm
for development purposes:

```
$ ffx driver run-tool fuchsia-pkg://fuchsia.com/uvc-tester#bin/uvc-tester
```
