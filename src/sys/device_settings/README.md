# `device_settings`

Reviewed on: 2019-07-22

`device_settings` exists to store mutable state for other components on the
system. It is currently deprecated in favor of [stash](../stash/README.md), and
new clients should favor stash over `device_settings`.

## Building

To add this project to your build, append `--with
//src/sys/device_settings` to the `fx set` invocation.

## Running

`device_settings` provides the `fuchsia.devicesettings.DeviceSettingsManager`
service on Fuchsia, and will be run by [sysmgr](../sysmgr/README.md) when other
components wish to access it.

## Testing

Unit tests for `device_settings` are available in the
`device_settings_manager_tests` package. This package is currently not included
in builds, as it has bit-rot and the tests do not pass.

## Source layout

The implementation is located in `src/main.rs`. Unit tests are co-located with
the implementation, and thus live in the same file.
