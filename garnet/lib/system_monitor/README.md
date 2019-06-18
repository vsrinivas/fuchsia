# System Monitor Dockyard

The System Monitor is a system for displaying the vital signs of a Fuchsia
device and its processes. Samples are collected for global and per-process
CPU, memory usage, and process list which are relayed to the Host computer
for display.

See also: /docs/development/system_monitor/README.md

### Dockyard Library

The Dockyard is used as a library (with no GUI itself) that collects data from
the Fuchsia device through the transport system. It responds to requests from
the GUI for digestible (i.e. small) sections of data that will be presented to
the user.

### Transport

The communication between the Harvester and the Dockyard is handled by the
Transport which will run on both the Host machine and the Fuchsia device.
