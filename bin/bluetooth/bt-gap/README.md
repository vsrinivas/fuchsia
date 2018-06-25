This directory contains the implementation of the public Bluetooth
APIs (`fuchsia.bluetooth*`). The main pieces of this
implementation are:
- [HostDevice](host_device.rs):
  - Receives FIDL events from `bt-host`, and relays them to mods via
    HostDispatcher.
  - Provides thin wrappers over some of the [private Bluetooth
    API](../../../../lib/bluetooth/fidl), for use by HostDispatcher.
- [ControlService](control_service.rs): Implements the `fuchsia.bluetooth.control.Control`
  interface, calling into HostDispatcher for help.
- [HostDispatcher](host_dispatcher.rs):
  - Implements all stateful logic for the `fuchsia.bluetooth.control.Control` interface.
  - Provides a Future to monitor `/dev/class/bt-host`, and react to the arrival
    and departure of Bluetooth hosts appropriately.
- [main](main.rs):
  - Binds the Control, Central, Peripheral, and Server FIDL APIs to code within
    this component (`bt-gap`).
    - The Control API is bound to ControlService.
    - Other APIs are proxied directly to their private API counterparts.
  - Instantiates the `/dev/class/bt-host`-monitoring future from HostDispatcher.
  - Configures an Executor to process API events and `/dev/class/bt-host` VFS events.
