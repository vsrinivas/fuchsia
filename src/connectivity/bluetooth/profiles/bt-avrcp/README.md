# Bluetooth Profile: AVRCP

This component implements the Audio Video Remote Control Profile (AVRCP) as
specified by the Bluetooth SIG in the
[official specification](https://www.bluetooth.org/docman/handlers/downloaddoc.ashx?doc_id=457082).

The `bt-avrcp` component is a system service that is created when needed -
typically as a side effect of launching the
[bt-a2dp-sink](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/bt-a2dp-sink/)
or
[bt-avrcp-target](https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/connectivity/bluetooth/profiles/bt-avrcp-target/)
components. Once launched, `bt-avrcp` will persist until explicitly terminated.

The component registers both the Controller (CT) and Target (TG) roles with the
BR/EDR
[Profile](https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.bluetooth.bredr/profile.fidl)
Service.

## Build configuration

Follow the steps for the A2DP sink profile and add this module to your build
with `--with //src/connectivity/bluetooth/profiles/bt-avrcp` to include AVRCP
with your build.

It's recommended to include the bt-avrcp-controller package to provide a simple
REPL to send AVRCP commands to peers either by including the bt tools package in
your build or directly with `--with
//src/connectivity/bluetooth/tools/bt-avrcp-controller`

eg: `fx set core.x64 --with //src/media/bundles:services --with
//src/connectivity/bluetooth/tools/bt-avrcp-controller --with
src/connectivity/bluetooth/profiles/bt-avrcp --with
//src/connectivity/bluetooth/profiles/bt-a2dp-sink`

## Using AVRCP from the shell

1.  AVRCP doesn't start automatically and service manager should launch the
    `bt_avrcp` service. The best way to do that today is by running the
    `bt-avrcp-controller` tool from the Fuchsia shell with a fake peer id and
    then immediately exiting the shell.

    Eg:

    ```
    #> bt-avrcp-controller 12345
    avrcp# exit
    >
    ```

1.  Start the `bt-a2dp-sink` service with `run -d
    fuchsia-pkg://fuchsia.com/bt-a2dp-sink#meta/bt-a2dp-sink.cmx`

1.  (If you haven't paired previously) pair your fuchsia device with your test
    device -

    -   Make the fuchsia device discoverable but running run `bt-cli` from the
        fuchsia shell.
    -   Use the `discoverable` command to make the fuchsia device discoverable
    -   Once your device is paired exit the bt-cli with the `exit` command.

1.  Obtain the peer id for the paired device

    -   Run the `bt-cli` from the fuchsia shell.
    -   Use the `list-peers` commands in bt-cli and copy down the peer ID.
    -   Exit bt-cli with the `exit` command.

1.  Run `bt-avrcp-controller <peer id>` (using the peer id from bt-cli) to
    obtain a controller for the peer.

1.  Issue commands to the peer using the AVRCP tool. Use the `help` command to
    get a list of supported commands available.

## Inspect

The `bt-avrcp` component includes support for
[component inspection](https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect). To view
the current state of the peers connected, use `fx iquery show bt-avrcp.cmx`.

### Hierarchy

```
 root:
      peers:
        peer_#:
          browse = (connected / connecting / disconnected)
          control = (connected / connecting / disconencted)
          peer_id

```

One peer child exists in the hierarchy for each peer that has been discovered, whether or not it
is connected.

## Code Layout

### `peer_manager` mod

#### `PeerManager`

*   Central hub of AVRCP
*   Routes requests from the FIDL front end services and from the BREDR service
    to the correct peer.

#### `TargetDelegate`

*   Functions as stateful proxy to the currently registered target handler and
    absolute volume handler.
*   Replies with error responses when the underlying handlers are not set.
*   Resolves outstanding notifications when target and absolute volume handlers
    close.
*   Typically used by the ControlCommandHandler for target profile commands.

### `peer` mod

#### `RemotePeerHandle`

*   Handle representing a remote peer.
*   Provides a public API for interacting and connecting to a remote peer and
    observing changes.
*   Typically used by `Controller`

#### `RemotePeer`

*   Internal state object representing peer
*   Contains the the discovered service attributes of the peer as reported by
    the BR/EDR service.
*   Owns any associated connections currently open to the peer.
*   Contains a vector of controller listener channels waiting for any incoming
    notifications by the peer. Events received by the peer are dispatched to all
    listening control listeners.

#### `Controller`

*   Public interface vended by peer manager to service and test frontends to
    communicate with a specific peer.
*   Encapsulates communication with peer manager to send commands requests to a
    peer and to handle the responses to those commands.
*   Provides an event stream to receive incoming notifications from the peer.

### `peer/tasks` mod

*   Provides a `state_watcher` task that is spawned to observe and react to
    state changes on each `RemotePeer` created
*   Provides connection statement management.
*   Makes outgoing connections to the remote peer when needed.
*   Spawns tasks to handle incoming events from the control and browse streams.
*   Owns a command handler (ControlCommandHandler) to respond to incoming AVC
    commands from the peer. Depending on the role we are functioning in after
    doing SDP discovery, this command handler will dispatch events to the target
    handler or to an absolute volume via the `TargetDelegate`

### `peer/handlers` mod

#### `ControlCommandHandler`

*   Handles AV\C commands received from the peer on the control channel.
*   Primarily used when when we are acting in AVRCP target role with A2DP source
    or for absolute volume handling with A2DP sink.
*   Maintains state for the peer as target for continuations and registered
    notifications by the peer.
*   Dispatches target commands to media session or a registered test target
    handler for most target commands.
*   Dispatches absolute volume commands to the registered absolute volume
    handler (typically A2DP sink).

### `service` mod

*   Encapsulates and abstracts (to facilitate testing and mocking) outward
    facing AVRCP FIDL services from the peer manager.
*   Handles FIDL request to obtain an AVRCP Controller for a given peer id.
*   Incoming requests for a controller to a peer are dispatched to the
    PeerManager who in turn vend off a PeerController to interact with the
    requested peer.
*   Handles setting over absolute volume handlers and test target handlers that
    will be registered into the PeerManager.

### `profile` mod

*   Encapsulates and abstracts (to facilitate testing) the BR/EDR FIDL service
    from the peer manager.
*   Sets up SDP service definitions that are registered with the BD/EDR service
    and service searches.
*   Vends off incoming L2CAP connections and discovered peer attributes events
    to the peer manager and gives the peer manager a method for making outgoing
    L2CAP connections to discovered peers.

### `packets` mod

*   Contains the AV\C control channel and browse channel vendor dependent
    specific packet encoders/decoders.
*   Provides methods for encoding the vendor dependent header/preambles on most
    commands.
*   Contains encoding logic for AV\C continuations/fragmentation in packet
    decoding and encoding of vendor dependent commands.
