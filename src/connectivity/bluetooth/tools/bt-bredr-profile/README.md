# bt-bredr-profile
`bt-bredr-profile` is a command-line front-end for the BR/EDR profile API ([fuchsia.bluetooth.bredr/Profile](../../../../../sdk/fidl/fuchsia.bluetooth.bredr/profile.fidl)).

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

Then run `fx run-test bt-bredr-profile-tests`.

## Commands
### connect
Targets `Profile.Connect`.

Issuing this command does not automatically connect non-connected peers, and
will fail for such peers. `bt-cli` may be used in conjunction with this tool to
control peer connections.

#### Usage
`connect <peer-id> <psm> <channel-mode> <max-rx-sdu-size> <security-requirements>`

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
profile> connect 75870b2c86d9e801 1 basic 672 none
Channel:
  Id: 0
  Mode: Basic
  Max Tx Sdu Size: 672
```

### disconnect
Drops the socket corresponding to `channel-id`, which will disconnect the l2cap
channel.

#### Usage
`disconnect <channel-id>`

##### Arguments
- `channel-id` is an integer assigned to this channel by the REPL.
It must correspond to a connected channel listed by the `channels` command.

### channels
Prints the assigned Ids of connected channels. These Ids are local to the REPL
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

### write
Write data on a socket/channel.

#### Usage
`write <channel-id> <data>`

##### Arguments
- `channel-id` is an integer assigned to a channel by the REPL. It must
  correspond to a connected channel listed by the `channels` command.
- `data` is a string of characters that will be written on the channel.

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
