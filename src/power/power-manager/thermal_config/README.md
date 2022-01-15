# Central thermal trip point configuration
This directory contains the central thermal trip point parser library and validator program.

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

## Format
The configuration begins with a top-level key, `clients`, which contains an entry for each
configured thermal client. The key name for each client entry must match the `client_type`
identifier that a client will specify in the call to `GetThermalState`.

The value for each client entry is an array of the client’s supported thermal states. A thermal
state is described by two keys:
* `state` - the thermal state value (unsigned integer) that this object
describes
* `trip_points` - an array of trip points that will activate this thermal state

A “trip point” is described by three keys:
* `sensor` - path to the sensor driver that this trip point corresponds to
* `activate_at` - the thermal load at which this trip point will become active
* `deactivate_below` - the thermal load at which this trip point becomes deactivated

## Theory of operation
### Client usage
A supported client may use the [fuchsia.thermal.GetThermalState](/sdk/fidl/fuchsia.thermal/) method
to query its current thermal state from the Power Manager. The state returned by this method will be
an integer corresponding to a specific trip point from that client's entry in the configuration
file. It is up to the client to implement actions resulting from thermal state changes.

### Power Manager usage
The configuration files are intended to be consumed directly by the Power Manager component. When
the Power Manager starts up, it will read in the configuration file from the filesystem. When a
client requests its current thermal state, the Power Manager will use available system temperature
information in combination with that client's trip point configuration to determine the appropriate
thermal state.

### Thermal state selection selection
The Power Manager is responsible for continuously monitoring temperature levels at each sensor. As
temperature increases into the range of a client’s configured trip points, the Power Manager must
determine the appropriate thermal state for the client, and communicate it to the client.

To select a client’s thermal state, the Power Manager will iterate over the client’s states and
associated trip points. If any of the state’s trip points are matched, then that thermal state is
chosen and the iteration will stop.