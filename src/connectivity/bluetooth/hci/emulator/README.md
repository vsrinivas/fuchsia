The bt-hci-emulator driver is a Bluetooth controller emulator that allows system
Bluetooth components to be tested in integration against the Bluetooth HCI
procotol.

bt-hci-emulator provides a standard HCI interface to the rest of the system while
allowing its behavior to be configured using an emulator interface. It
accomplishes this by publishing two distinct devices:

* A device of class "bt-emulator". This device allows the emulator's behavior to
  be configured using the
  [fuchsia.bluetooth.test.HciEmulator](//sdk/fidl/fuchsia.bluetooth/test/hci_emulator.fidl)
  protocol (also see
  [fuchsia.bluetooth.hardware.bluetooth.Emulator](//zircon/system/fidl/fuchsia-hardware-bluetooth/hci.fidl)).
* A device of class "bt-hci". See
  [fuchsia.hardware.bluetooth.Hci](//zircon/system/fidl/fuchsia-hardware-bluetooth/hci.fidl)
  and [ddk.protocol.bt-hci](//zircon/system/banjo/ddk.protocol.bt.hci/bt-hci.banjo).

## Usage
TODO(fxbug.dev/822): Document the driver's device publishing behavior once implement,
with a usage example.

## Library Utilities
TODO(fxbug.dev/822): Document Rust library utilities.
