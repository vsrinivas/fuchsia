# Central thermal trip point configuration

This directory contains the central thermal trip point parser library and validator program.

## Context

The purpose of the central thermal trip point configuration is to create a file where the trip point
configuration for all clients in the system can be defined in a single place. Having all trip points
in a single location aims to provide a few benefits:
1. Improved maintainability.
2. Easier reasoning about a system’s holistic thermal throttling scheme.
3. Better alignment with the general direction of central configuration owned by the Power Manager,
   similar to the low-power mode configuration system.
4. Improved ability to support per-sensor trip point configurations, which will be needed to support
   more advanced throttling policies, and isn’t achievable with the current thermal interface

## Example configuration

```
{
    clients: {
        audio: [
            {
                state: 1,
                trip_points: [
                    {
                        sensor: '/dev/sys/platform/05:03:a/thermal',
                        activate_at: 75,
                        deactivate_below: 71,
                    },
                ],
            },
            {
                state: 2,
                trip_points: [
                    {
                        sensor: '/dev/sys/platform/05:03:a/thermal',
                        activate_at: 86,
                        deactivate_below: 82,
                    },
                ],
            },
        ],
    },
}
```

## Format

The configuration begins with a top-level key, `clients`, which contains an entry for each
configured thermal client. The key name for each client entry must match the `client_type`
identifier that a client will specify in the call to
[`fuchsia.thermal.ClientStateConnector/Connect`](/sdk/fidl/fuchsia.thermal/client_state.fidl;l=40;drc=002657ec7305d38bfeaea7e1d5f10f3952367238).

The value for each client entry is an array of the client’s supported thermal states. A thermal
state is described by two keys:
* `state` - the thermal state value (unsigned integer) that this object
describes
* `trip_points` - an array of trip points that will activate this thermal state

A “trip point” is described by three keys:
* `sensor` - path to the sensor driver that this trip point corresponds to
* `activate_at` - the thermal load at which this trip point will become active
* `deactivate_below` - the thermal load at which this trip point becomes deactivated

## Adding board support

To add support for a new board:
1. Create a new board-specific JSON5 file, taking care to follow the correct format (detailed below)
2. In your new JSON5 file, add a client entry and associated trip points for each thermal client you
   wish to support
3. Define a [thermal_config](/src/power/power-manager/thermal_config/thermal_config.gni) build
   target for the new JSON5 file and include it in the respective board-specific gni file

## Validation

The [thermal_config](/src/power/power-manager/thermal_config/thermal_config.gni) GN template
implements a built-in validator. The validator runs at compile time as long as the `thermal_config`
target is included in the build graph. It will ensure 1) the file is valid JSON5, and 2) the JSON5
format adheres to the expected format (defined by the parsing library
[here](/src/power/power-manager/thermal_config/parser/lib.rs)).

## Raw temperature vs thermal load

Trip points are described in terms of "thermal load" instead of raw temperatures. Therefore, if trip
points based on raw temperatures are desired, those raw temperatures must first be converted into
equivalent thermal load values.

*__Background on thermal load__ – “thermal load” is simply an integer in the range [0..100] which
represents the severity of thermals at a given sensor. A thermal load value of 0 corresponds to
“thermally unthrottled” while a value of 100 corresponds to “max thermal throttling” and typically
results in a system shutdown. Thermal load is calculated as an interpolation between a “start” and
“end” temperature (corresponding to “throttling onset” and “emergency reboot” temperatures,
respectively).*

To convert the desired raw temperature trip points into corresponding thermal load trip points, we
must first choose “onset” and “reboot” threshold temperatures for each sensor, which will be
configured internally in the Power Manager’s node configuration (example:
[Nelson](/src/power/power-manager/node_config/nelson_node_config.json;l=86-108;drc=f56ecd713a17da5949ed7e270db887586a6573e1)).
Once the “onset” and “reboot” temperature thresholds are determined, we can apply the following
formula to convert the desired raw temperature for each trip point into the equivalent thermal load
value:

`thermal_load = (T - T_onset) / (T_reboot - T_onset) * 100`

## Trip point configuration

The specific trip point configuration for each client does not follow any one-size-fits-all
approach. Typically, it is the responsibility of the system integrator interested in thermal
management to determine the appropriate thermal trip points for each client. The configuration
should consider:
* how many thermal throttle states are supported by a given client
* when should throttling be applied for a client relative to other clients
* when should throttling be applied for a client relative to emergency thermal reboot

## Theory of operation

### Client usage

A supported client may use the
[`fuchsia.thermal.ClientStateConnector/Connect`](/sdk/fidl/fuchsia.thermal/client_state.fidl;l=40;drc=002657ec7305d38bfeaea7e1d5f10f3952367238)
method to connect a
[`fuchsia.thermal.ClientStateWatcher`](/sdk/fidl/fuchsia.thermal/client_state.fidl;l=54;drc=002657ec7305d38bfeaea7e1d5f10f3952367238)
channel to the Power Manager, which can then be used to query its current thermal state. The state
returned by this method will be an integer corresponding to a specific trip point from that client's
entry in the configuration file. It is up to the client to implement actions resulting from thermal
state changes.

### Power Manager usage

The configuration files are intended to be consumed directly by the Power Manager component. When
the Power Manager starts up, it will read in the configuration file from the filesystem. When a
client requests its current thermal state, the Power Manager will use available system temperature
information in combination with that client's trip point configuration to determine the appropriate
thermal state.

### Thermal state selection

The Power Manager is responsible for continuously monitoring temperature levels at each sensor. As
temperature increases into the range of a client’s configured trip points, the Power Manager must
determine the appropriate thermal state for the client, and communicate it to the client.

To select a client’s thermal state, the Power Manager will iterate over the client’s states and
associated trip points. If any of the state’s trip points are matched, then that thermal state is
chosen and the iteration will stop.