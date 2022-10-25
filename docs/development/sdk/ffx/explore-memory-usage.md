# Explore the memory usage

`ffx profile memory` is a tool to explore the RAM usage of a Fuchsia system.
It works by evaluating how much commited memory is used by VMOs on the system, regardless of whether these VMOs are mapped or not (unlike `ps`).

### Getting the raw data

Under the hood, this tool uses the component `memory_monitor` to capture information on all the VMOs of the system.

You can get the raw data exported by `memory_monitor` with the option `--debug-json`

### Measuring data over time

You can track the memory usage over time by combining the `--csv` and  `--interval` options, for example:
```
ffx profile memory --csv --interval 1
```
