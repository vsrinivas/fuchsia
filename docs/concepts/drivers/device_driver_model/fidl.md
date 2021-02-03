# FIDL in drivers

Devices may implement Interfaces, which are
[FIDL](/docs/development/languages/fidl/README.md) RPC protocols
that clients (services, applications, etc) use. The base device interface
supports POSIX style open/close/read/write IO. Interfaces are supported through
the `message()` operation in the base device interface.

Examples -
* [GPIO interface](/sdk/fidl/fuchsia.hardware.gpio/gpio.fidl)
* [Ethernet interface](/sdk/fidl/fuchsia.hardware.ethernet/ethernet.fidl)
