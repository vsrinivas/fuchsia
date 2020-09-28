## bt-avdtp-tool

`bt-avdtp-tool` sends AVDTP commands to a peer that is connected using the `fuchsia.bluetooth.avdtp`
protocol of a running component. This tool supports both A2DP sink and source.

The primary use of this tool is to provide user prompted commands to a
Fuchsia device under test for passing PTS certification tests.

Before running, make sure you've included the tool and the a2dp profiles in your `fx set`.
```
--with //src/connectivity/bluetooth/profiles/bt-a2dp-sink
--with //src/connectivity/bluetooth/profiles/bt-a2dp-source
--with //src/connectivity/bluetooth/tools/bt-avdtp-tool
```
Please look at the README's for a2dp-sink and a2dp-source for any additional
dependencies that may be needed.

Note: `bt-avdtp-tool` launches the required A2DP component. If an A2DP component
is already running (`bt-a2dp-sink.cmx` or `bt-a2dp-source.cmx`), stop it before
following the instructions below.

1) Launch `$ bt-cli` and make sure the adapter is discoverable using `discoverable`.
2) In a different shell, for A2DP sink: `$ bt-avdtp-tool -p sink`. For A2DP source:
`$ bt-avdtp-tool -p source`. This should spawn a command line interface for sending avdtp
commands to the peer.
3) On the PTS machine, run a test. Make sure the device address entered in PTS matches
the device address shown in the `bt-cli` tool.

* To see the available commands and their descriptions, type `help` in the CLI.
* To see how to use a specific command, type `help _CommandName_` in the CLI.
* Note that each avdtp-tool command must be proceeded by a generic id. See the output
of `fx syslog` for the mapping of peer to generic id. The generic id starts at 0, and
is incremented after every PTS test that is run. TODO(fxbug.dev/37089): Deprecate this id scheme and
use the underlying `peer_id`.
* TODO(aniramakri): A command `peers` printing the list of generic ids mapped to the connected peers.

This tool is not meant to be used in a production environment; it is sending out-of-band
AVDTP commands to the A2DP profile. This can cause A2DP to get into a bad or irrecoverable state.
It is recommended to restart the tool every few commands to refresh A2DP back to its
original state.