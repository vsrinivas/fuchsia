# System Monitor

```
This is not yet part of garnet/packages/all. If you'd like to see the
work-in-progress version, please add system_monitor to your build packages with
something like the following, which will explicitly add this to your build:

$ fx set x64 --packages \
  garnet/packages/all,garnet/packages/tools/disabled/system_monitor
```

The System Monitor is a system for displaying the vital signs of a Fuchsia
device and its processes. Samples are collected for global and per-process CPU,
memory usage, and process list which are relayed to the Host computer for
display.

## Main Components

The main components of the System Monitor execute as separate processes (either
on the Host computer or the Fuchsia device).

There are several components of the System Monitor
- GUI
- Dockyard
- Harvester
- Transport

### GUI

At the highest level, the GUI displays samples collected from the Fuchsia device
(in graphs or charts as appropriate), which come from the Dockyard on the Host.
The GUI for the System Monitor is not included in this source directory. It is
implemented at a higher level (or petal).

### Dockyard Library

The Dockyard is used as a library (with no GUI itself) that collects data from
the Fuchsia device through the transport system. It responds to requests from
the GUI for digestible (i.e. small) sections of data that will be presented to
the user.

### Harvester

The Harvester runs on the Fuchsia device, acquiring Samples (units of
introspection data) that it sends to the Host using the Transport system. The
Harvester does not store samples.

### Transport

The communication between the Harvester and the Dockyard is handled by the
Transport which will run on both the Host machine and the Fuchsia device.
