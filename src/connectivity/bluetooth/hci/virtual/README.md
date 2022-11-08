The bt-hci-virtual driver is a Bluetooth controller emulator that allows system
Bluetooth components to be tested in integration against the Bluetooth HCI
procotol.

bt-hci-virtual provides aa method to add virtual HCI controllers to the system
which can either be emulated, or loopback the HCI commands to a component.

It accomplishes this by publishing three distinct devices:

* A device of class "bt-emulator". This device allows the emulator's behavior to
  be configured using the
  [fuchsia.bluetooth.test.HciEmulator](//sdk/fidl/fuchsia.bluetooth.test/hci_emulator.fidl)
  protocol (also see
  [fuchsia.hardware.bluetooth.Emulator](//sdk/fidl/fuchsia.hardware.bluetooth/hci.fidl)).
* A device of class "bt-loopback". See
  [fuchsia.bluetooth.test.Loopback](//sdk/fidl/fuchsia.bluetooth.test/loopback.fidl).

## Usage
TODO(fxbug.dev/822): Document the driver's device publishing behavior once implement,
with a usage example.

## Library Utilities
TODO(fxbug.dev/822): Document Rust library utilities.
