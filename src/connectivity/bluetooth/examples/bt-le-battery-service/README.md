# BT LE Battery Service

This example demonstrates how to publish the system's battery level in a GATT battery service.
When run, it adds the service to the local device database. It then responds to clients
that make requests to this GATT service.


## Build

The `bt-le-battery-service` relies on the `fuchsia.bluetooth.gatt2.Server` capability to publish a
GATT server. It also relies on the `fuchsia.power.battery.BatteryManager` capability to receive
updates about the system's battery level.

Add the example to your Fuchsia configuration. Note, because the component is eagerly started, make
sure to include the package in the cached set of packages. For example, for the workstation
configuration, add the `bt-le-battery-service` to the [legacy_cache_package_labels](/products/common/workstation.gni).

Include the example [core_shard](meta/bt-le-battery-service.core_shard.cml) in your target
product configuration. For example, for the workstation configuration, add the core
shard to the `core_realm_shards` list in [workstation.gni](/products/common/workstation.gni).

## Component configuration

The security level of the published GATT battery service can be configured via
[structured configuration](https://fuchsia.dev/fuchsia-src/development/components/configuration/structured_config).
Update the [default configuration file](config/default.json5) with the desired security level.

## Component startup

The example `bt-le-battery-service` is implemented as a Component Framework v2 component. This
component is eagerly started on device boot. To start the service, reboot the device.
