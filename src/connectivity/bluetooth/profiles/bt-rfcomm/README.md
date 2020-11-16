# Bluetooth Protocol: RFCOMM

This component implements the RFCOMM Protocol version 1.2 as specified by the Bluetooth SIG in the
[official specification](https://www.bluetooth.org/docman/handlers/DownloadDoc.ashx?doc_id=263754).

The RFCOMM component provides the BR/EDR
[Profile](../../../../../sdk/fidl/fuchsia.bluetooth.bredr/profile.fidl) Service. Local clients
should use the `Profile` service to search, advertise, and connect to any RFCOMM services.

* When advertising an RFCOMM service using the `Profile.Advertise` FIDL method, no Server Channel
  number is required in the service definition. The RFCOMM component will reserve and assign a
  Server Channel for the service. Any Server Channel number provided in the
  `protocol_descriptor_list` or `additional_protocol_descriptor_lists` of the service definition
  will be overwritten.
* When attempting to establish an outbound RFCOMM connection using the `Profile.Connect` FIDL
  method, the client should include the `RfcommChannel` number in the `ConnectParameters`. This
  number is synonymous with the Server Channel number found in the Protocol Descriptor Lists of
  the service definition.
* There are no special considerations when searching for an RFCOMM service using the
  `Profile.Search` FIDL method.

## Build Configuration

Add the following to your Fuchsia set configuration to include the component:

`--with //src/connectivity/bluetooth/profiles/bt-rfcomm`

To run the component:

1. `fx shell`
1. `run bt-rfcomm.cmx`

## Testing

RFCOMM relies on unit tests to validate behavior. Add the following to your Fuchsia set
configuration to include the component unit tests:

`--with //src/connectivity/bluetooth/profiles/bt-rfcomm:bt-rfcomm-tests`

To run the tests:

```
fx test bt-rfcomm-tests
```

## Inspect

The `bt-rfcomm` component includes support for
[component inspection](https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect). To view
the current state of the RFCOMM server, use `fx iquery show bt-rfcomm_*/bt-rfcomm.cmx`.

### Hierarchy

```
 root:
      rfcomm_server:
        peer_#:
          connected = (Connected / Disconnected)

```

One peer child exists in the hierarchy for each RFCOMM Session between the local endpoint and remote
peer, whether or not it is currently active.

## Code Layout

The layout provides an abstraction between the logic for managing active `Profile` service requests
and the core RFCOMM functionality. Important modules and their recommended entry points are listed
below.

## `profile_registrar` mod

The `profile_registrar` module manages all the active service advertisements, searches, and
connection requests from any local clients that require the aforementioned BR/EDR `Profile` service.
Any `Profile` request that does not require RFCOMM will be forwarded directly to the upstream
[`Profile Server`](../../core/bt-host/fidl/profile_server.h). The main entry point to this module is
the [`ProfileRegistrar`](src/profile_registrar.rs) which directly manages the RFCOMM services.

## `rfcomm` mod

The `rfcomm` module contains the relevant handlers, datatypes, and functionality needed to
support creating and receiving RFCOMM connections. The `ProfileRegistrar` will route any
inbound/outbound requests to the `RfcommServer`, which manages the active RFCOMM Sessions
between the local endpoint and any remote peers. The main entry point to this module is the
[`RfcommServer`](src/rfcomm/server.rs).

### `frame` mod

The `frame` module contains the packet definitions for all of the supported RFCOMM commands.
It includes encoding and decoding definitions to convert to & from byte buffers containing
RFCOMM data. The module defines the data types used to store RFCOMM commands and responses - these
types are used throughout the `rfcomm` mod. The main entry point to this module is the
[`Frame`](src/rfcomm/frame/mod.rs) object.

### `session` mod

The `session` module contains the business logic for managing an RFCOMM Session between the
local endpoint and a remote peer. It contains a handler for reading incoming data from the
remote peer, parsing into a valid RFCOMM frame, and doing the necessary RFCOMM work.
The main entry point to this module is the [`Session`](src/rfcomm/session/mod.rs) object.
