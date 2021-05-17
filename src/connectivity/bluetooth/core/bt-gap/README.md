This directory contains the implementation of the public Bluetooth
APIs (`fuchsia.bluetooth*`). The main pieces of this
implementation are:
- [HostDevice](src/host_device.rs):
  - Receives FIDL events from `bt-host`, and relays them to mods via
    HostDispatcher.
  - Provides thin wrappers over some of the [private Bluetooth API](/src/connectivity/bluetooth/fidl/host.fidl), for use by HostDispatcher.
- [AccessService](src/services/access.rs): Implements the `fuchsia.bluetooth.sys.Access`
   interface, calling into HostDispatcher for help.
- [HostDispatcher](src/host_dispatcher.rs):
  - Implements all stateful logic for the `fuchsia.bluetooth.sys.Access` interface.
  - Provides a Future to monitor `/dev/class/bt-host`, and react to the arrival
    and departure of Bluetooth hosts appropriately.
- [main](src/main.rs):
  - Binds the Access, Central, Peripheral, and Server FIDL APIs to code within
    this component (`bt-gap`).
    - The Access API is bound to AccessService.
    - Other APIs are proxied directly to their private API counterparts.
  - Instantiates the `/dev/class/bt-host`-monitoring future from HostDispatcher.
  - Configures an Executor to process API events and `/dev/class/bt-host` VFS events.
  - Serves `bt-gap` Inspect file at `/hub/c/bt-gap.cmx/{pid}/out/diagnostics/root.inspect`.
