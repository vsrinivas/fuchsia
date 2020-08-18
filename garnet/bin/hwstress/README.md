# `hwstress`

`hwstress` is a tool for exercising hardware components, such as CPU, RAM, and
flash. It can be used both to test that hardware is correctly functioning
(revealing bad RAM, bad flash, or system heating problems), and also as a system
load generator (for example, running the CPU at 50% utilization and consuming
50% of RAM).

## Usage

```
hwstress <subcommand> [options]

Attempts to stress hardware components by placing them under high load.

Subcommands:
  cpu                    Perform a CPU stress test.
  flash                  Perform a flash stress test.
  light                  Perform a device light / LED stress test.
  memory                 Perform a RAM stress test.

Global options:
  -d, --duration=<secs>  Test duration in seconds. A value of "0" (the default)
                         indicates to continue testing until stopped.
  -v, --verbose          Show additional logging.
  -h, --help             Show this help.

CPU test options:
  -u, --utilization=<percent>
                         Percent of system CPU to use. A value of
                         100 (the default) indicates that all the
                         CPU should be used, while 50 would indicate
                         to use 50% of CPU. Must be strictly greater
                         than 0, and no more than 100.
  -w, --workload=<name>  Run a specific CPU workload. The full list
                         can be determined by using "--workload=list".
                         If not specified, each of the internal
                         workloads will be iterated through repeatedly.

Flash test options:
  -c, --cleanup-test-partitions
                         Cleanup all existing flash test partitions in the
                         system, and then exit without testing. Can be used
                         to clean up persistent test partitions left over from
                         previous flash tests which did not exit cleanly.
  -f, --fvm-path=<path>  Path to Fuchsia Volume Manager.
  -m, --memory=<size>    Amount of flash memory to test, in megabytes.

Memory test options:
  -m, --memory=<size>    Amount of RAM to test, in megabytes.
  --percent-memory=<percent>
                         Percentage of total system RAM to test.
```

## Test details

### CPU

The CPU stress test will run various workloads across all the CPUs in the
system. The workloads are a mix of integer and floating point arithmetic, and
have basic checks that performed calculations were correct.

### RAM

The RAM stress tests attempt to:

*   Find bad bits in RAM by writing patterns (both random data and deterministic
    patterns) and verifying that they can be correctly read back again.

*   Find RAM affected by the [Row hammer][rowhammer] vulnerability.

[rowhammer]: https://en.wikipedia.org/wiki/Row_hammer

### Flash

The flash stress tests writes unique values to each sector of flash, and
verifies that the written data can be correctly read back again.

The flash tests create a raw partition to allow direct communication to the
flash device, bypassing Fuchsia's filesystem layers. To do this safely, the test
will only exercise block devices managed by [FVM][fvm]. The test will create a
raw partition, exercise the partition, and then remove the partition when
finished:

```sh
# Create and test a 100MiB partition on block device 000.
hwstress flash --fvm-path=/dev/class/block/000/fvm --memory=100
```

If the test is aborted before finishing, the test partition may not be cleaned
up, and the space allocated to the partition not freed. The partition will be
removed if the system is rebooted, or by running:

```sh
# Remove all hwstress test partitions in the system.
hwstress flash --cleanup-test-partitions
```

[fvm]: https://fuchsia.dev/fuchsia-src/glossary#fuchsia-volume-manager

## See also

*   `loadgen` (`src/zircon/bin/loadgen`) which has many threads performing
    cycles of idle / CPU work, useful for exercising the kernel's scheduler.

*   `kstress` (`src/zircon/bin/kstress`) which is designed to stress test the
    kernel itself.
