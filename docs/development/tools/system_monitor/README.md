# System Monitor

The System Monitor is a system for displaying the vital signs of a Fuchsia
device and its processes. Samples are collected for global and per-process CPU,
memory usage, and process list which are relayed to the Host computer for
display.

## Main Pieces

There are several pieces of the System Monitor

- GUI or CLI program
- [Dockyard Library](dockyard/README.md)
- Transport
- [Harvester](harvester/README.md)

### GUI or CLI program

At the highest level, the UI displays samples collected from the Fuchsia device
(in graphs or charts as appropriate), which come from the Dockyard on the Host.
The GUI for the System Monitor is not included in this source directory. It is
implemented at a higher level.

### Transport

The communication between the Harvester and the Dockyard is handled by the
Transport. The Transport (which is made up of libraries, source code, and
protocol definitions) is used on both the Host machine and the Fuchsia device.
