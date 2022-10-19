# BT Fast Pair Example Client

This example demonstrates how to enable Fast Pair Provider functionality on the local device.
When run, it enables Fast Pair and processes any Fast Pair & Normal pairing requests from Bluetooth
peers.

## Build

The `bt-fastpair-client` relies on the `fuchsia.bluetooth.fastpair.Provider` capability to enable
the service. This is provided by the `bt-fastpair-provider` component. Follow [these instructions](/src/connectivity/bluetooth/profiles/bt-fastpair-provider/README.md)
to include it in the build.

It also relies on the `fuchsia.bluetooth.sys.Pairing` capability to claim the
Pairing Delegate on the local device. Because only one delegate can be claimed at a time, use a
build that disables all system Bluetooth clients (sometimes called an "arrested" build).

Add `bt-fastpair-client` to your Fuchsia configuration. Because the example component is eagerly
started, include the package in the cached set of packages. For example, for the workstation
configuration, add `bt-fastpair-client` to the [legacy_cache_package_labels](/products/common/workstation.gni).

Include the example [core_shard](meta/bt-fastpair-client.core_shard.cml) in your target
product configuration. For example, for the workstation configuration, add the core
shard to the `core_realm_shards` list in [workstation.gni](/products/common/workstation.gni).

## Component startup

The example `bt-fastpair-client` is implemented as a Component Framework v2 component. This
component is eagerly started on device boot. To start the example, reboot the device. To disable
Fast Pair, terminate the running `bt-fastpair-client` component.
