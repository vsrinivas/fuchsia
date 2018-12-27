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

## Terms

#### Logical Sample Value
A mathematically generated summary of 0 to many (physical) Samples. E.g. if we
render 3 columns on the screen for 12 actual Samples, each Logical Sample Value
will be computed from 4 actual Samples. If no scaling is done by GUI, then each
Logical Sample will naturally align with one column of the rendered graph.

#### Normalize
#### Sample
The physical Samples that taken together make up a Logical Sample.

#### Sample Set
A portion of a |Sample Stream|.

#### Sample Category
The logical, named idea of where Samples come from. E.g. "cpu0", "physMem",
"procCount" are Sample Categories.

#### Sample Stream
An ordered set of Samples for a given Sample Category. E.g. all the Samples for
"cpu0".

#### Sample Stream ID
#### Sample Stream Name
#### Smooth

#### Request
#### Response


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
