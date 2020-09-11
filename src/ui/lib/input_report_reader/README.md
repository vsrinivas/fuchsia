# Input Report Reader

This directory contains the Input Report Reader, a library which binds to
the [InputReport Drivers](/sdk/fidl/fuchsia.input.report/)
and parses their reports into
[fuchsia.ui.input:InputReports](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.ui.input/input_reports.fidl).

## USAGE

This program cannot be run directly. It exists in the system
as a library used by [`RootPresenter`](/src/ui/bin/root_presenter/README.md).

This library should not be used in new programs, as [fuchsia.ui.input:InputReports](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.ui.input/input_reports.fidl) is deprecated.

### TEST

This program has the following test command:

Interface test:
```
fx test input-report-reader-test
```
## Class Organization

`InputReader` is the top-level class that is created and holds all other classes.
`InputReader` is responsible for creating the device watchers for the system.
At the moment we only have one type of `DeviceWatcher`, `FdioDeviceWatcher`.
`FdioDeviceWatcher` watches for new InputReport devices. These devices appear as files
in the `/dev/class/input-report/` directory.

When `FdioDeviceWatcher` discovers a new device, it creates a channel to the device.
This channel is then passed through a callback up to `InputReader`.
`InputReader` creates an `InputInterpreter` with the new channel.

The `InputInterpreter` class is the class that drives the sending and receiving
of reports from the Driver. `InputInterpreter` gets a Report Descriptor
and parses the data into the form necessary for RootPresenter.
If the Report Descriptor contains a Touch device, ConsumerControl device, or Mouse device,
then the correct `fuchsa.ui.input:InputDevicePtr` will be allocated to send
reports and descriptors to RootPresenter.

### Class Relationships

* An `InputReader` has exactly one `DeviceWatcher`.
* An `InputReader` may have one or more `InputInterpreter` classes.
* An `InputInterpreter` has exactly one `zx::channel` representing a driver.
