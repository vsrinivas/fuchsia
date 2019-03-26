# CPU Performance Monitoring

CPU Performance Monitoring uses hardware provided counters to collect
statistics on how well the system is performing.

Currently only Intel chips are supported.

On Intel chips this includes the performance counters of the cpu. See Intel
Volume 3 Chapters 18,19 "Performance Monitoring" for architectural details.
It also includes other counters that may be provided.
For example on Intel the memory controller provides statistics on
memory bytes read and written.

Data collection is driven by the "cpuperf" program described herein.
Data can also be collected with Fuchsia's general tracing support
using [the cpuperf provider](../../../docs/development/tracing/cpuperf-provider.md).

## Quick Start

Data is collected with the "cpuperf" program to drive the "trace session"
(not to be confused with Fuchsia's general tracing system, driven by
[the "trace" program](../trace/README.md)).

The specification of what data to collect is provided by a "cpspec" file
(pronounced "See Pee Spek"). The contents and format of this file
are described below. Several examples are provided by the cpuperf package.

Example:

```shell
$ cpuperf --spec-file=/pkgfs/packages/cpuperf/0/data/basic-cpu-memory.cpspec
[INFO:main.cc(209)] cpuperf control program starting
[INFO:main.cc(210)] 100 iteration(s), 1 second(s) per iteration
...
```

N.B. That particular spec requires a very wide terminal.

## Events

Items of data are collectively called "events".
Events are divided into "groups".
The choice of grouping is largely driven by the architecture.
Intel provides support for architectural and model-specific counters.
In addition while there is a plethora of counters that can collect data,
only a few may be active at any one time. Apart from those programmable
counters are a few "fixed" counters that don't take up precious
"programmable counter" slots.

### Fixed Event Group

The Fixed Event Group is specified by group name "fixed".

There are three fixed events/counters on Intel chips:

instructions_retired:
This event counts retired instructions.

unhalted_core_cycles:
This event counts unhalted core cycles.

unhalted_reference_cycles:
This event counts unhalted reference cycles.

### Architectural Event Group

The Architectural Event Group is specified by group name "arch".

Architectural Events are common across all generations of the architecture
(from the date of introduction of course).

Architectural events are specified with the programmable event counters.

Intel chips provide the following architectural events:

*instructions_retired*:
This is the same as the *instructions_retired* fixed event except that it
is specified with a programmable event counter.

*unhalted_core_cycles*:
This is the same as the *unhalted_core_cycles* fixed event except that it
is specified with a programmable event counter.

*unhalted_reference_cycles*:
This is the same as the *unhalted_reference_cycles* fixed event except that it
is specified with a programmable event counter.

*llc_references*:
This event counts Last Level Cache references.

*llc_misses*:
This event counts Last Level Cache misses.

*branches_retired*:
This event counts retired branches.

*branch_misses_retired*:
This event counts retired branch misses.

### Model-specific Event Group

The Model Event Group is specified by group name "model".

Model-specific events are, obviously, specific to the model of the cpu.
On Intel, Skylake and Kaby Lake chips generally support the same events.
To see what events are supported, run `cpuperf --list-events`.

Model-specific events are specified with the programmable event counters.

### Miscellaneous Event Group

The Miscellaneous Event Group is specified by group name "misc".

Miscellaneous events include the memory controller read/write counts.
To see what events are supported, run `cpuperf --list-events`.

### Limitations of how much can be collected

Intel chips provide support for collecting more than 100 different events.
However, the hardware only supports 4 active counters at a time
(or 8 without hyperthreading).
In addition, any and all fixed counters may be used by any configuration.
There are currently no limitations on the number of miscellaneous events
that may be collected, with the qualification that at most a total of
32 events across all groups may be specified.

## Sampling vs Tally modes

There are two basic modes of operation: sampling and tally.

They are distinguished by the sample rate: Tally mode has a sample rate
of zero.

### Sampling Mode

In sampling mode data is collected at a specified frequency.
On Intel, each event counter (fixed and programmable) is set to a pre-specified
value and is (effectively) counted down each time the event occurs. When
the count reaches zero a PMI interrupt (Performance Monitor Interrupt)
is generated at which point data is collected.

This is effectively what happens. In actuality, the counters count up
and when the counter overflows the PMI is generated. But it's easier to
reason about the counters by thinking they count down, and the API
works this way.

Some events, like all the miscellaneous events, can't be collected this way.
They are not hooked up to generate a PMI. Instead, a separate counter
called the "timebase" is used: when that counter triggers a PMI then
data for the misc counter is collected. For simplicity there can be only
one timebase, and for simplicity the timebase counter is always the
first specified event. See below for details on how to specify a timebase.

### Tally Mode

In tally mode data is collected cumulatively across the entire traces
session, and the only data written to the trace buffer are the final
counts. This mode is useful for providing a macroscopic view of performance
in realtime, as the data can be easily and quickly reported after each
iteration.

## Session Configuration

Configuration a trace run is done by writing a json file describing:

- what events to collect
- how often to collect data
- ancillary data to collect with each event
- how long to collect data for
- how many iterations to do
- the size of the buffer to record data
- where to store the results

The format of the specification file has the following schema:

```json
{
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "config_name": {
      "type": "string"
    },
    "model_name": {
      "type": "string"
    },
    "events": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "group_name": {
            "type": "string"
          },
          "event_name": {
            "type": "string"
          },
          "rate": {
            "type": "integer"
          },
          "flags": {
            "type": "array",
            "uniqueItems": true,
            "items": {
              "type": "string",
              "enum": [
                "os",
                "user",
                "pc",
                "timebase0"
              ]
            }
          },
          "required": [ "group_name", "event_name" ]
        }
      }
    },
    "buffer_size_in_mb": {
      "type": "integer",
      "minimum": 1
    },
    "duration": {
      "type": "integer",
      "minimum": 0
    },
    "num_iterations": {
      "type": "integer",
      "minimum": 1
    },
    "output_path_prefix": {
      "type": "string"
    },
    "session_result_spec_path": {
      "type": "string"
    },
    "required": [ "events" ]
  }
}
```

Some values have defaults.

 - `model_name`: obtained from `perfmon::GetDefaultModelName()`
 - `output_path_prefix`: "/tmp/cpuperf"
 - `session_result_spec_path`: "/tmp/cpuperf.cpsession"

### Example Specification

This spec collects data every 10,000 retired instructions.

```json
// Basic cpu and memory stats, reported in "tally mode".
{
  "config_name": "printer-test",
  "events": [
    {
      "group_name": "fixed",
      "event_name": "instructions_retired",
	  "rate": 10000,
      "flags": [ "os", "user" ]
    },
    {
      "group_name": "fixed",
      "event_name": "unhalted_reference_cycles",
      "flags": [ "os", "user", "timebase0" ]
    },
    {
      "group_name": "arch",
      "event_name": "llc_references",
      "flags": [ "os", "user", "timebase0" ]
    },
    {
      "group_name": "arch",
      "event_name": "llc_misses",
      "flags": [ "os", "user", "timebase0" ]
    },
    {
      "group_name": "misc",
      "event_name": "memory_bytes_read",
      "flags": [ "timebase0" ]
    },
    {
      "group_name": "misc",
      "event_name": "memory_bytes_written",
      "flags": [ "timebase0" ]
    }
  ],
  "buffer_size_in_mb": 16,
  "duration": 10,
  "num_iterations": 1,
  "output_path_prefix": "/tmp/cpuperf-test",
  "session_result_spec_path": "/tmp/cpuperf-test.cpsession"
}
```

## Result Format

Collected trace data is written to files, one per cpu per iteration.
The files are named ${prefix}.<iteration-number>.<cpu-number>.cpuperf.

To simplify processing of results, metadata describing the result is
written to a separate file: ${prefix}.cpsession, which has the following
schema:

```json
{
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "config_name": {
      "type": "string"
    },
    "num_iterations": {
      "type": "integer",
      "minimum": 1
    },
    "num_traces": {
      "type": "integer",
      "minimum": 1
    },
    "output_path_prefix": {
      "type": "string"
    }
  }
}
```

## Pretty-printer

For testing, debugging, and development purposes the program
`cpuperf_print` will pretty-print traces.
At present this program is only available on the development host.

Example:

```shell
$ out/x64/cpuperf_print --session=/path/to/downloaded.cpsession
<pretty printed output>
```

In the future this program may do more complex forms of processing
of the trace session result.
