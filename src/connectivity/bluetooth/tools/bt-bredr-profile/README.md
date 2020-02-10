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
### connect-l2cap
Targets `Profile.ConnectL2cap`.

Issuing this command does not automatically connect non-connected peers, and
will fail for such peers. `bt-cli` may be used in conjunction with this tool to
control peer connections.

#### Usage

`connect-l2cap <peer-id> <psm> <channel-mode> <max-rx-sdu-size>`

##### Arguments
- `peer-id` maps to the `peer_id` field of `ConnectL2cap`
- `psm` maps to the `psm` field of `ConnectL2cap`
- `channel-mode` can be one of {`basic`, `ertm`}, and maps to the
  `parameters.channel_mode` field of `ConnectL2cap`
- `max-rx-sdu-size` maps to the `parameters.max_rx_sdu_size` field of `ConnectL2cap`

#### Example
```
profile> connect-l2cap 75870b2c86d9e801 1 basic 672
Channel:
  Id: 0
  Mode: Basic
  Max Tx Sdu Size: 672
```

### channels
Prints the assigned Ids of connected channels. These Ids are local to the REPL
and are only used for indicating which channel to perform operations on in other commands.

Usage:

`channels`

Example:

```
profile> channels
Channel:
  Id: 0
  Mode: Basic
  Max Tx Sdu Size: 672
```
