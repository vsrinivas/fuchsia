# Harvester

The Harvester runs on the Fuchsia device, acquiring Samples (units of
introspection data) that it sends to the Host using the Transport system. The
Harvester does not store samples.

The Harvester relies on kernel APIs (to gather data) and a Transport layer (to
transmit data to the Dockyard).

See also: [System Monitor](../README.md)

## Samples

Various kinds of data are collected by the Harvester

- [CPU Samples](cpu_samples.md)
- [Memory Samples](memory_samples.md)
- [Task Samples](task_samples.md)
- [Component Introspection](component_introspection.md)
