# CPU Performance Monitor

## Introduction

The CPU Performance Monitor Trace Provider gives the user access to the
performance counters built into the CPU using the
[tracing system provided by Fuchsia](../trace/README.md).

At present this is only supported for Intel chipsets.

On Intel the Performance Monitor provides the user with statistics regarding
many aspects the CPU.
For a complete list of the performance events available for, e.g.,
Skylake chips see Intel Volume 3 Chapter 19.2,
Performance Monitoring Events For 6th And 7th Generation Processors.
Not all events (or "counters") are currently available, there's a lot(!),
but hopefully a number of useful events are currently present.

Here are a few examples:

- cache hits/misses, for each of L1, L2, L3
- cycles stalled due to cache misses
- branch mispredicts
- instructions retired

The tracing system uses "categories" to let one specify what trace data
to collect. Cpuperf uses these categories to simplify the specification
of what h/w events to enable. The full set of categories is listed below.

To collect trace data, run `trace record` on your Fuchsia system,
or indirectly via the `traceutil` host tool. The latter is recommended
as it automates the download of the collected "trace.json" file to your
desktop.

Example:

```
bash$ out/debug-x86-64/host_x64/traceutil record \
  --buffer-size=64 \
  --duration=2s \
  --categories=gfx,cpu:fixed:instructions_retired,cpu:fixed:unhalted_reference_cycles,cpu:l2_lines,cpu:sample:10000
Starting trace; will stop in 2 seconds...
Stopping trace...
Trace file written to /data/trace.json
Downloading trace... done
Converting trace-2017-11-12T17:55:45.json to trace-2017-11-12T17:55:45.html... done.
```

After you have the `.json` file on your desktop you can load it into
`chrome://tracing`. If you are using `traceutil` an easier way to view
the trace is by loading the corresponding `.html` file that `traceutil`
generates. The author finds it easiest to run `traceutil` from the top level
Fuchsia directory, view that directory in Chrome (e.g.,
`file:///home/dje/fnl/ipt/fuchsia`), hit Refresh after each new trace
and then view the trace file in a separate tab.

## Basic Operation

The basic operation of performance data collection is to allocate a
buffer for trace records for each CPU, and then set a counter (on each CPU)
to trigger an interrupt after a pre-specified number of events occurs.
This interrupt is called the PMI interrupt (Performance Monitor Interrupt).
On Intel the interrupt triggers when the counter overflows, at which point
the interrupt service routine will write various information (for example
timestamp and program counter) to the trace buffer, reset the counter
to re-trigger another interrupt after the pre-specified number of events,
and return.

When tracing stops the buffer is read by the Cpuperf Trace Provider and
converted to the trace format used by the Trace Manager.

Tracing also stops when the buffer fills. How much trace data can be
collected depends on several factors:

- duration of the trace
- size of the buffer
- frequency of sampling
- how frequently the counter overflows
- whether program counter information is written to the buffer

## Data Collection Categories

As stated earlier, the Fuchsia tracing system uses "categories" to
let one specify what data to collect. For CPU tracing, there are categories
to specify what counters to enable, whether to trace the os, userspace,
or both, as well as specify the sampling frequency.

For each performance counter see the Intel documentation for further
information. This document does not attempt to provide detailed information
on each counter.

### Sample Rate

Data for each counter is collected at a rate specified by the user.
Eventually specifying a random rate will be possible. In the meantime
the following set of rates are supported:

- cpu:sample:100
- cpu:sample:500
- cpu:sample:1000
- cpu:sample:5000
- cpu:sample:10000
- cpu:sample:50000
- cpu:sample:100000
- cpu:sample:500000
- cpu:sample:1000000

Note that, currently, each counter is sampled independently.
For example, if one requests "cpu:fixed:instructions_retired"
and "arch:llc" (Last Level Cache - L3) with a sampling rate of 10000,
then retired instructions will be sampled every 10000 "instruction retired"
events and LLC operations will be sampled every 10000 "LLC" events,
with the former happening far more frequently than the latter.
Timestamps are collected with each sample so one can know how long it took
to, for example, retire 10000 instructions.

Later, it will be possible to specify a single timebase (a single counter),
say unhalted reference cycles, and collect all samples at the same time.
This can provide a more consistent view of what's happening. On the other hand,
doing so means we forego collecting statistical pc data for each event
(since the only pc values we will have are those for the timebase event).

### Options

- cpu:os - collect data for code running in kernelspace.

- cpu:user - collect data for code running in userspace.

- cpu:profile_pc - collect pc data associated with each event

This is useful when wanting to know where, for example, cache misses
are generally occurring (statistically speaking, depending upon the
sample rate). The address space and program counter of each sample
is included in the trace output. Doing so doubles the size of each
trace record though, so there are tradeoffs.

### Fixed Counters

The Intel Architecture provides three "fixed" counters:

- cpu:fixed:instructions_retired

- cpu:fixed:unhalted_core_cycles

- cpu:fixed:unhalted_reference_cycles

These counters are "fixed" in the sense that they don't use the programmable
counters. There are three of them and each of them has a fixed use.
The advantage of them is that they don't use up a programmable counter:
There are dozens of counters but, depending on the model, typically only
at most four are usable at a time.

### Programmable Counters

There are dozens of programmable counters on Skylake (and Kaby Lake) chips.
For a complete list see Intel Volume 3 Chapter 19.2,
Performance Monitoring Events For 6th And 7th Generation Processors.
For a list of the ones that are currently supported see
`zircon/system/public/zircon/device/cpu-trace/intel-pm-events.inc`
and
`zircon/system/public/zircon/device/cpu-trace/skylake-pm-events.inc`
in the source tree.

To simplify specifying the programmable counters they have been grouped
into categories defined in
`zircon/system/public/zircon/device/cpu-trace/intel-pm-categories.inc`
and
`zircon/system/public/zircon/device/cpu-trace/skylake-pm-categories.inc`
in the source tree. See these files for a full list.

Only one of these categories may be specified at a time.
[Later we'll provide more control over what data to collect.]

A small selection of useful categories:

- arch:llc
  - Last Level Cache (L3) references
  - Last Level Cache (L3) misses

- arch:branch
  - Branch instructions retired
  - Branch instructions mispredicted

- skl:l1_summary
  - Number of outstanding L1D misses every cycle
  - Number of outstanding L1D misses for any logical thread on this processor core
  - Number of lines brought into L1 data cache

- skl:l2_summary
  - Demand requests that missed L2
  - All requests that missed L2
  - All Demand Data Read requests to L2
  - All requests to L2

- skl:l3_summary
  - Requests originating from core that reference cache line in L3
  - Cache miss condition for references to L3

- skl:offcore_demand_code
  - Incremented each cycle of the number of offcore outstanding Demand Code Read transactions in SQ to uncore
  - Cycles with at least 1 offcore outstanding Demand Code Read transactions in SQ to uncore

- skl:offcore_demand_data
  - Incremented each cycle of the number of offcore outstanding Demand Data Read transactions in SQ to uncore
  - Cycles with at least 1 offcore outstanding Demand Data Read transactions in SQ to uncore
  - Cycles with at least 6 offcore outstanding Demand Data Read transactions in SQ to uncore

- skl:l1_miss_cycles
  - Cycles while L1 data miss demand load is outstanding
  - Execution stalls while L1 data miss demand load is outstanding

- skl:l2_miss_cycles
  - Cycles while L2 miss demand load is outstanding
  - Execution stalls while L2 miss demand load is outstanding

- skl:l3_miss_cycles
  - Cycles while L3 miss demand load is outstanding
  - Execution stalls while L3 miss demand load is outstanding

- skl:mem_cycles
  - Cycles while memory subsystem has an outstanding load
  - Execution stalls while memory subsystem has an outstanding load

Note: The wording of some of these events may seem odd.
The author has tried to preserve the wording found
in the Intel manuals, though improvements are welcome.

Note: This is just a first pass! They'll be reworked
as the need arises. Please see the categories.inc files
in your source tree for an up to date list.
