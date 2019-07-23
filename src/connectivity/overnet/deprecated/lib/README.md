Overnet
=======

Overnet provides:
- Datagram delivery over a wide variety of media with optional reliability and ordering
- An overlay mesh network across devices of some security domain (e.g. all "my" devices)
- Proxying of various protocols over this overlay network (one of these protocols is FIDL)

Source Code:
- [overnetstack](overnetstack) - The main overnet daemon run on Fuchsia, exporting a [FIDL interface](../../../../sdk/fidl/fuchsia.overnet/overnet.fidl) for application use.
- [examples](examples) - Various example programs
- [tools](tools) - Tools for adminstering an Overnet mesh
- [lib](lib) - Protocol stack libraries
