# BT Device Identification Client

This example demonstrates how to set the identification of a Fuchsia device. The device
identification will be advertised via Bluetooth.

## Build Configuration

The `bt-device-id-client` component relies on the `fuchsia.bluetooth.deviceid.DeviceIdentification`
protocol, which is implemented by the `bt-device-id` component.

Add the following to your Fuchsia set configuration to include the implementing component and
example.

```
--with //src/connectivity/bluetooth/profiles/bt-device-id
--with //src/connectivity/bluetooth/examples/bt-device-id-client
```

Include the bt-device-id [core shard](/src/connectivity/bluetooth/profiles/bt-device-id/meta/bt-device-id.core_shard.cml)
in your target product configuration. For example, for the workstation configuration, add the core
shard to the `core_realm_shards` list in [workstation.gni](/products/common/workstation.gni).

## Component Startup

The example `bt-device-id-client` is implemented as a Components Framework v2 component.
Consequently, running the component requires the [ffx](https://fuchsia.dev/reference/tools/sdk/ffx)
tool.

Before running the example, update the capability route for the `DeviceIdentification` protocol by
adding an `offer` declaration to the [ffx laboratory core shard](/src/developer/remote-control/meta/laboratory.core_shard.cml):

```
offer: [
    {
        protocol: [ "fuchsia.bluetooth.deviceid.DeviceIdentification" ],
        from: "#bt-device-id",
        to: "#ffx-laboratory",
    },
]
```

This makes the `DeviceIdentification` capability accessible by components started in the `ffx` realm.

To run the example:

```
ffx component run /core/ffx-laboratory:bt-device-id-client fuchsia-pkg://fuchsia.com/bt-device-id-client#meta/bt-device-id-client.cm
```
