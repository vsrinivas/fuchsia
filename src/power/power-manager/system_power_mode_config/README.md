# System power mode configuration and operation

This directory contains the system power mode configuration parser library and validator program.
The Power Manager will use the parser library to read in the configuration JSON5 file, which it will
use to control the operating modes (i.e., power levels) of configured client services in response to
changing system power modes.

## Example configuration
```
{
    clients: {
        wlan: {
            mode_matches: [
                {
                    mode: 'battery_saver',
                    power_level: 0,
                },
            ],
            default_level: 1,
        },
    },
}
```

## Static configuration

The configuration file begins with a top-level key, `clients`, which contains an entry for each
statically-configured client type. The key name for each client entry must be a lower snake case
representation of a valid
[`fuchsia.power.clientlevel.ClientType`](/sdk/fidl/fuchsia.power.clientlevel/clientlevel.fidl)
variant.

The value for each client entry is an object containing two keys:
* `mode_matches` - an array of
  [`fuchsia.power.systemmode.ModeMatch`](/sdk/fidl/fuchsia.power.systemmode/systemmode.fidl/)
  entries
* `default_level` - the uint64 that gets sent to the client if `mode_matches` contained no matches

Each `ModeMatch` entry consists of:
* `mode` - a
  [`fuchsia.power.systemmode.SystemMode`](/sdk/fidl/fuchsia.power.systemmode/systemmode.fidl/) value
  indicating the match criterion for this `ModeMatch` entry. If this mode is contained in the
  currently active system power modes, then this `ModeMatch` is considered a match.
* `power_level` - the uint64 that gets sent to the client if this entry is a match

## Dynamic configuration

A privileged component can freely update the static configuration for any client using the
[`fuchsia.power.systemmode.ClientConfigurator`](/sdk/fidl/fuchsia.power.systemmode/systemmode.fidl/)
protocol. When the configuration for a client is changed, the Power Manager will reevaluate the
current power level for that client as detailed in the [Power level
selection](#power-level-selection) section below. This protocol could also be used to add
configuration for a client that did not originally exist in the static configuration.

## Adding static configuration for a new product

To add a static configuration file for a new product:
1. Create the new product-specific JSON5 file, taking care to follow the correct format (detailed
   below)
2. In your new JSON5 file, add a client entry and associated configuration for each power client you
   wish to support
3. Define a
   [`system_power_mode_config`](/src/power/power-manager/system_power_mode_config/system_power_mode_config.gni)
   build target for the new JSON5 file and include it in the respective product-specific `.gni` file

## Static config validation

The
[`system_power_mode_config`](/src/power/power-manager/system_power_mode_config/system_power_mode_config.gni)
GN template implements a built-in validator. The validator runs at compile time as long as the
`system_power_mode_config` target is included in the build graph. It will ensure 1) the file is
valid JSON5, and 2) the JSON5 format adheres to the expected format (defined by the parsing library
[here](/src/power/power-manager/system_power_mode_config/parser/lib.rs)).

## Power levels

Each client type (WLAN, Display, etc.) will internally manage its own set of supported power levels.
The definition and implementation of each power level may be product-specific and is ultimately up
to the client to manage. However, it is recommended that a client structure its power levels in
order of increasing power use, beginning with level 0. The thinking is that the power level index
should increase with power usage, which is likely more conceptually intuitive than the reverse
ordering, and also subtly urges clients to favor a lower default state of 0 (lower power operation).

## Client usage

A client service which has an existing configuration may use the
[`fuchsia.power.clientlevel.Connector`](/sdk/fidl/fuchsia.power.clientlevel/clientlevel.fidl)
protocol to open a
[`fuchsia.power.clientlevel.Watcher`](/sdk/fidl/fuchsia.power.clientlevel/clientlevel.fidl) channel
with the Power Manager. After the `Watcher` has been connected, a client may use the `Watcher.Watch`
method to watch for changes to its power level which occur as a result of changes in the system
power modes. The Power Manager will communicate the new power level values to connected clients, at
which point those clients should take care to update their operating modes appropriately.

## Power Manager operation

The configuration files are intended to be consumed directly by the Power Manager component. When
the Power Manager starts up, it will read in the system power mode configuration file from the
filesystem. When a client requests its current power level, the Power Manager will determine the
correct level by referencing the currently active system power modes in combination with that
client's configuration.

## Power level selection {#power-level-selection}

The power level for a client will be reevaluated any time A) the system power mode has been changed
via
[`fuchsia.power.systemmode/Requester.Request`](/sdk/fidl/fuchsia.power.systemmode/systemmode.fidl/),
or B) that client’s configuration has been changed via
[`fuchsia.power.systemmode/ClientConfigurator.Set`](/sdk/fidl/fuchsia.power.systemmode/systemmode.fidl/).
To select the appropriate power level for a client, the Power Manager will traverse the
`mode_matches` entries for that client. If the `mode` specified by an entry is contained in the
currently active system power modes, then the corresponding `power_level` from the entry will be
selected as this client’s current power level. If there are no matches, then `default_level` will be
selected.

This approach allows the product configurator to:
* Disambiguate between conflicting system power modes
* Control the ordering of mode matching / level selection
* Explicitly specify a default power level
