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

#### Filtered Sample
A filtered sample is a representation of actual samples. E.g. a filtered sample
might have the value 900 if filter was averaging actual samples of 700, 800,
1000, and 1100. Note that the filtered sample doesn't match any of the actual
samples in this example.

#### Logical Sample Value
A mathematically generated summary of 0 to many (physical) Samples. E.g. if we
render 3 columns on the screen for 12 actual Samples, each Logical Sample Value
will be computed from 4 actual Samples. If no scaling is done by GUI, then each
Logical Sample will naturally align with one column of the rendered graph.

#### Normalize
A graph rendering filter.
Scales the graph output so that the high and low values are framed nicely on
the screen. The data is scaled dynamically.

#### Sample Stream Request
A message sent from the GUI to the dockyard. It specifies the time range,
sample streams, and filtering desired.

#### Sample Stream Response
A message sent from the dockyard to the GUI to answer a Sample Stream Request.
It includes the filtered sample data (a Sample Set) for each of the requested
sample streams.

#### Sample
The physical Samples that taken together make up a Logical Sample.

#### Sample Set
A portion of a |Sample Stream|. E.g. a Sample Stream might have 100,000 Samples
and a Sample Set might only show filtered view of a 1,200 of them.

#### Sample Category
The logical, named idea of where Samples come from. E.g. "cpu", "memory",
"process" are Sample Categories.

#### Sample Stream
An ordered set of Samples for a given Sample Category. E.g. all the Samples for
"cpu0".

#### Sample Stream ID
A number used to uniquely refer to a Sample Stream. The Sample Stream ID is 1:1
with a specific Sample Stream Name.

#### Sample Stream Name
A text string referring to a Sample Stream. The Name value is primarily used in
the GUI (for humans), internally the Sample Stream ID is used. E.g. "cpu0",
"physMem", "procCount" are Sample Stream Names.

#### Smooth
A graph rendering filter.
Blends neighboring samples to create a graph with less noise.


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
