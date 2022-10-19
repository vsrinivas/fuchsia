# bt-avrcp-controller

`bt-avrcp-controller` is a command-line front end for the AVRCP Peer Manager API ([fuchsia.bluetooth.avrcp.PeerManager](/sdk/fidl/fuchsia.bluetooth.avrcp/controller.fidl)).
The tool supports sending commands over both the AVRCP Control and Browse channels.

# Build

Include `bt-avrcp-controller` in the build. For example, if using `fx set`:
```
--with //src/connectivity/bluetooth/tools/bt-avrcp-controller
```

Include the [testonly AVRCP core shard](/src/connectivity/bluetooth/profiles/bt-avrcp/meta/bt-avrcp-testonly.core_shard.cml)
in the build. For example, if using `fx set`:

```
--args='core_realm_shards += [ "//src/connectivity/bluetooth/profiles/bt-avrcp:bt-avrcp-testonly-core-shard" ]'
```

The tool can be started in `fx shell`:
`fx shell bt-avrcp-controller $PEER_ID`
