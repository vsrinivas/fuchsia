# HID Input Reader

This directory contains the Input Reader, a service which binds to
Human Input Devices
[(HID Devices)](https://www.usb.org/sites/default/files/documents/hid1_11.pdf)
and parses their reports into
[InputReports](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.ui.input/input_reports.fidl).

## USAGE

This program cannot be run directly. It exists in the system
as a library used by the Input Stack.

### TEST

This program has the following two test commands:

Interface test:
```
fx run-test scenic_tests -t input_apptests
```

Unit test:
```
fx run-test scenic_tests -t input_reader_unittests
```

## How to Add Support For a New Device

1. Add a new `Protocol` representing your device (`protocols.h`).
2. Add the logic to select the protocol in `ExtractProtocol` (`input_interpreter.cc`).
3. Create a new `Device` class with the ability to parse a Report Descriptor and a
   valid report (New File).
4. Allocate the new Device in `ParseHidReportDescriptor` (`input_interpreter.cc`).
5. Add Device level unit tests (New File).
6. Add one or more interface tests (`end_to_end_tests.cc`).

## Class Organization

`InputReader` is the top-level class that is created and holds all other classes.
`InputReader` is responsible for creating the device watchers for the system.
At the moment we only have one type of `DeviceWatcher`, `FdioDeviceWatcher`.
`FdioDeviceWatcher` watches for new HID devices. These devices appear as files
in the `/dev/class/input/` directory.

When `FdioDeviceWatcher` discovers a new device, it creates a `FdioHidDecoder`.
The `FdioHidDecoder` is then passed through a callback up to `InputReader`.
`InputReader` creates an `InputInterpreter` with the new `HidDecoder`.

The `HidDecoder` class is responsible for sending and receiving HID reports.
The name "HidDecoder" is for historical reasons, it should eventually be renamed to
"HidReader". `HidDecoder` signals when its device has a new Report that needs
to be read. It also has the ability to send reports back to the device.

The `InputInterpreter` class is the class that drives the sending and receiving
of reports from `HidDecoder`. When `InputInterpreter` gets a Report Descriptor
from `HidDecoder`, it decides which device class the device represents.
It will then allocate the `Device` necessary to read and interpret the
Report Descriptor and Report.

Each `Device` class represents a single input device. For example, there are
Touchscreen, Touchpad, Mouse, and Stylus Device classes. A single file in
`/dev/class/input` can end up creating multiple `Device` classes. For example,
a touchscreen HID device creates both a Touchscreen device and a Stylus device.
In general, a single HID report ID is associated with a single `Device` class.

### Class Relationships

* An `InputReader` has a one-to-one relationship with `DeviceWatcher`.
* An `InputReader` has a one-to-many relationship with `InputInterpreter` classes.
* An `InputInterpreter` has a one-to-many relationship with `Device` classes.
* An `InputInterpreter` has a one-to-one relationship with `HidDecoder`.
* A `HidDecoder` has a one-to-one relationship with the HID device.
