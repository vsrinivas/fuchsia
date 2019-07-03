## Core Bluetooth system library

This library provides a management plane for Fuchsia's core Bluetooth subsystem.
It defines protocols that build an abstraction for the Generic Access Profile.

The FIDL protocols defined in this library allow clients to manage hardware
policy, read and write sensitive information (such as device name), intercept
authentication challenges, and manage stored bonds. Hence these capabilities
should only be granted to components that have sufficient privilege.

### Protocols

* [HostWatcher](./host_watcher.fidl): Fuchsia maintains a host-subsystem for
  each Bluetooth controller that is available to the OS, represented as a
  [bt-host](//src/connectivity/bluetooth/core/bt-host) device. The HostWatcher
  protocol can be used to enumerate the bt-host devices that are detected by the
  core system and designate an active one that all Bluetooth procedures will be
  routed to.

* [Access](./access.fidl): Abstracts the procedures defined in the Generic
  Access Profile including device discovery, connection establishment, and
  pairing. This protocol is intended to build system-level user interfaces.
