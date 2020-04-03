# Fuchsia DevTools

Fuchsia DevTools is a tool for displaying the vital signs of a Fuchsia device
and its processes. Samples are collected for global and per-process CPU, memory
usage, and process list. Samples are then relayed to the host machine for
display.

Fuchsia DevTools has the following parts:

-   [GUI or CLI program](#gui-or-cli-program)
-   [Dockyard library](#dockyard-library)
-   [Transport](#transport)
-   [Harvester](#harvester)

## GUI or CLI program {#gui-or-cli-program}

At the highest level, the UI displays samples collected from the Fuchsia device
(in graphs or charts as appropriate), which come from the Dockyard on the host.
The GUI for Fuchsia DevTools is not included in this source directory. It is
implemented at a higher level.

## Dockyard library {#dockyard-library}

The [Dockyard](dockyard/README.md) is a library that collects data from the
Harvester running on the Fuchsia device.

## Transport

The communication between the Harvester and the Dockyard is handled by the
Transport. The Transport (which is made up of libraries, source code, and
protocol definitions) is used on both the host machine and the Fuchsia device.

## Harvester

The [Harvester](harvester/README.md), running on the Fuchsia device, acquires
samples (units of introspection data) and sends them to the host machine using
the Transport system.
