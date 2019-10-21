## bt-avdtp-tool

`bt-avdtp-tool` sends AVDTP commands to a peer that is connected using the `fuchsia.bluetooth.avdtp`
protocol of a running component (currently, bt-a2dp-sink).

The primary use case of this tool is to provide user prompted commands to a
Fuchsia device under test for passing PTS certification tests.

Before running, make sure you've included the tool and a2dp-sink in your `fx set`.
```
--with //src/connectivity/bluetooth/profiles/bt-a2dp-sink
--with //src/media/audio/bundles:audio --with //src/media/playback/mediaplayer
--with //src/connectivity/bluetooth/tools/bt-avdtp-tool
```

Note: `bt-avdtp-tool` launches the required `bt-a2dp-sink` component. If `bt-a2dp-sink`
is already running, stop it before following the instructions below.

1) Launch `$ bt-cli` and make sure the adapter is discoverable using `discoverable`.
2) In a different shell, `$ bt-avdtp-tool`. This should spawn a command line
interface for sending avdtp commands to the peer.
3) On the PTS machine, run a test. Make sure the device address entered in PTS matches
the device address shown in the `bt-cli` tool.

To see the available commands and their descriptions, type `help` in the CLI.
Note that each avdtp-tool command must be proceeded by a generic id. See the output
of `fx syslog` for the mapping of peer to generic id. A list of generic ids mapped
to the connected peers is also available through the `peers` command.