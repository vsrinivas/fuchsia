# bt-bredr-profile
`bt-bredr-profile` is a command-line front-end for the BR/EDR profile API ([fuchsia.bluetooth.bredr/Profile](/sdk/fidl/fuchsia.bluetooth.bredr/profile.fidl)).
The tool supports establishing both L2CAP and RFCOMM channels, and provides an interface for sending
data over these channels.

## Build
Include `bt-bredr-profile` in your `fx set`:
```
--with //src/connectivity/bluetooth/tools/bt-bredr-profile
```

## Test
Include the `tests` target in your `fx set`:

```
--with //src/connectivity/bluetooth/tools/bt-bredr-profile:tests
```

Then run `fx test bt-bredr-profile-tests`.

## Commands
### setup-rfcomm
Registers an example RFCOMM-requesting SPP service. Adds a service advertisement and search.

#### Usage
`setup-rfcomm`

### connect-l2cap
Targets `Profile.Connect`.

Issuing this command does not automatically connect non-connected peers, and
will fail for such peers. `bt-cli` may be used in conjunction with this tool to
control peer connections.

#### Usage
`connect-l2cap <peer-id> <psm> <channel-mode> <max-rx-sdu-size> <security-requirements>`

##### Arguments
- `peer-id` maps to the `peer_id` field of `Connect`
- `psm` maps to the `psm` field of `Connect`
- `channel-mode` can be one of {`basic`, `ertm`}, and maps to the
  `parameters.channel_mode` field of `Connect`
- `max-rx-sdu-size` maps to the `parameters.max_rx_sdu_size` field of `Connect`
- `security-requirements` can be one of {`none`, `auth`, `sc`, `auth-sc`}, and maps to the
  `parameters.security_requirements` field of `Connect`.
#### Example
```
profile> connect-l2cap 75870b2c86d9e801 1 basic 672 none
Channel:
  Id: 0
  Mode: Basic
  Max Tx Sdu Size: 672
```

### connect-rfcomm
Attempts to make an outgoing RFCOMM connection to the service advertised by `server-channel`
on the peer indicated by `peer-id`.

Issuing this command does not guarantee a successful RFCOMM connection. Users should
only attempt to make a connection to a peer whose services have been discovered - the
`server-channel` for any such service will be printed in the REPL.

#### Usage
`connect-rfcomm <peer-id> <server-channel>`

##### Arguments
- `peer-id` maps to the `PeerId` of the peer.
- `server-channel` maps to the Server Channel number of the peer's advertised RFCOMM service.
#### Example
```
profile> connect-rfcomm 75870b2c86d9e801 1
```

### disconnect-l2cap
Drops the socket corresponding to `channel-id`, which will disconnect the l2cap
channel.

#### Usage
`disconnect-l2cap <channel-id>`

##### Arguments
- `channel-id` is an integer assigned to this channel by the REPL.
It must correspond to a connected channel listed by the `channels` command.

### disconnect-rfcomm
Drops the socket corresponding to `server-channel`, which will disconnect the rfcomm
channel connected to the remote peer.

#### Usage
`disconnect-rfcomm <peer-id> <server-channel>`

##### Arguments
- `peer-id` maps to the `PeerId` of the peer.
- `server-channel` is the Server Channel number assigned to the RFCOMM channel.

### disconnect-rfcomm-session
Closes the RFCOMM Session with the remote peer identified by `peer-id`.

#### Usage
`disconnect-rfcomm-session <peer-id>`

##### Arguments
- `peer-id` maps to the `PeerId` of the peer.

### channels
Prints the assigned Ids of connected L2CAP channels. These Ids are local to the REPL
and are only used for indicating which channel to perform operations on in other commands.

#### Usage
`channels`

#### Example
```
profile> channels
Channel:
  Id: 0
  Mode: Basic
  Max Tx Sdu Size: 672
```

### write-l2cap
Write data on an L2CAP socket/channel.

#### Usage
`write-l2cap <channel-id> <data>`

##### Arguments
- `channel-id` is an integer assigned to a channel by the REPL. It must
  correspond to a connected channel listed by the `channels` command.
- `data` is a string of characters that will be written on the channel.

### write-rfcomm
Write data on the RFCOMM channel identified by `server-channel`.

#### Usage
`write-rfcomm <peer-id> <server-channel> <data>`

##### Arguments
- `peer-id` maps to the `PeerId` of the peer.
- `server-channel` is the integer Server Channel number identifying the RFCOMM channel. For
  channels that were established by the peer, use the number printed by the tool in `fx log`.
  For channels that were initiated by the tool, use the same channel number as used in the
  `connect-rfcomm` command.
- `data` is a string of characters that will be written on the channel.

### send-remote-line-status
Send a remote line status update to the remote peer on the RFCOMM channel identified
by `server-channel`. By default, a Framing Error status will be sent.

#### Usage
`send-remote-line-status <peer-id> <server-channel>`

##### Arguments
- `peer-id` maps to the `PeerId` of the peer.
- `server-channel` is the integer Server Channel identifying the RFCOMM channel. For
  channels that were established by the peer, use the identifier printed in the REPL.
  For channels that were initiated by the tool, use the same identifier as used in the
  `connect-rfcomm` command.

### advertise
Targets `Profile.Advertise`.

Advertises an L2CAP service with the SDP server. After advertising, a
notification will be printed when a peer connects to that service.

#### Usage
`advertise <psm> <channel-mode> <max-rx-sdu-size>`

For convenience, a valid Audio Sink service definition (with 1 UUID and 1 L2CAP
protocol descriptor) is created from just the `psm` argument.

##### Arguments
- `psm` maps to the PSM included in the L2CAP protocol descriptor of the service definition.
- `channel-mode` maps to `parameters.channel_mode` and is {`basic`|`ertm`} (which may be abbreviated to their first
  letter).
- `max-rx-sdu-size` maps to `parameters.max_rx_sdu_size` and is an integer in the
  range 0 - 65535.

#### Example

```
profile> advertise 25 ertm 672
Service:
  Id: 0
```

### remove-service

Unregisters an L2CAP service from the SDP server, using the id returned by
`advertise`

#### Usage
`remove-service <service-id>`

##### Arguments
- `service-id` is the identifier assigned to the registration after executing
  the `advertise` command.

### services
Lists services registered with the `advertise` command.

#### Usage
`services`

#### Example
```
profile> services
Service:
  Id: 0
  Mode: Basic
  Max Rx Sdu Size: 48
```

### exit / quit
Removes all services, closes all open channels, and exits the REPL.
