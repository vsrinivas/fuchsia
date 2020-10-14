# Loadbench: Configurable Workload Simulator

## Introduction

Loadbench is a tool for stressing the system in interesting ways to validate
performance, latency, and stability under load. It provides a simple JSON-based
configuration language to describe workload behavior, including interactions
with kernel objects, synchronization primitives, and timing.

The tool is intended to support the following use cases:
* Manually testing kernel and system behavior, in conjunction with tracing, to
  observe and validate correctness and performance under directed stress.
* Providing a framework for automated load metrics and stress testing.
* Supporting development of a suite of regression tests to capture minimal
  reproduction cases of issues found in real world applications.

## General Usage

The loadbench package is not included in any standard product build. To use the
tool for local manual testing, add the following to your `fx set` command:

```bash
$ fx set <other args> --with-base //src/testing:testing
```

Once the tool is included in a build and flashed to the device, it can be driven
from the commandline using the `run` command:

```bash
$ fx shell run loadbench --help
```

Use the `--file <path>` option to specify a JSON workload specification file to
execute:

```bash
$ fx scp ./my_test_load.json "[$(fx get-device-addr)]:/tmp/"
$ fx shell run loadbench --file /tmp/my_test_load.json
```

Note: `/tmp/r/sys/fuchsia.com:loadbench:0#meta:loadbench.cmx` will not be present
until the loadbench package is run for the first time after device boot. To
explicitly create this directory from the commandline use:

```bash
$ fx shell mkdir -p /tmp/r/sys/fuchsia.com:loadbench:0\#meta:loadbench.cmx/
```

Workload specification files can also be built into the package. This is useful
for workloads intended for automated and regression testing. These files can be
run manually by referencing them relative to the `/pkg/data/` path prefix:

```bash
$ fx shell run loadbench --file /pkg/data/simple_load.json
```

## Workload Specification Files

Workload specification files are standard JSON format, with extensions to allow
trailing commas and single `// ...` and multi-line `/* ... */` comments.

The primary document element of a specification file is always a JSON object:

```JSON
{
  // Top-level specification members ...
}
```

### Top-Level Specification Members

The primary document element may contain any of the following members.

#### name - Workload Name (optional)

The *name* member is an optional string to print when running the workload. The
value of this member must be a string.

Example:
```JSON
{
  "name": "Example workload name string."
  // ...
}
```

#### config - Workload Config Options (optional)

The *config* member is an optional JSON object that may contain the following
options:
* *priority*: Integer specifying the priority to set the main control thread to
  before kicking off the worker threads. This may be used to prevent high
  priority workers from delaying the main control thread.
* *interval*: Specifies the duration to run the benchmark. See _duration spec_
  for permitted values and formats.

Example:
```JSON
{
  "config": {
    "priority": 25,
    "interval": "30s",
  }
  // ...
}
```

#### intervals - Named Intervals (optional)

The *intervals* member is an optional JSON object that specifies named intervals
that worker threads may specify as parameters to time-based actions. The key of
each member of this object specifies the interval's name, while the value
is an object that specifies its duration.

The duration object may have one of the following key/value pairs:
* *duration*: Specifies the numeric duration of the interval.
* *uniform*: A JSON object that specifies the min and max durations to select
  from a random uniform distribution.

See _duration spec_ for permitted values and formats.

Example:
```JSON
{
  "intervals": {
    "fast inverval": { "duration": "250us" },
    "short random interval": { "uniform": { "min": "1ms", "max": "5ms" } },
    "long random interval": { "uniform": { "min": "10s", "max": "20s" } },
  },
  // ...
}
```

#### objects - Global Objects (optional)

The *objects* member is an optional JSON object that specifies named objects
that worker threads may interact with. The key of each member of this object
specifies the object's name, while the value is a JSON object that specifies the
object's type and parameters.

Example:
```JSON
{
  "objects": {
    "main timer": { "type": "timer" },
    "port a": { "type": "port" },
    "port b": { "type": "port" },
  },
  // ...
}
```

The following types are currently supported:
* *channel*: Zircon channel object.
* *event*: Zircon event object.
* *port*: Zircon port object.
* *timer*: Zircon timer object.

Other object types and parameters will be introduced over time.

#### behaviors - Global Behaviors (optional)

The *behaviors* member is an optional JSON object that specifies named behaviors
that may be referenced by worker threads. Named behaviors make it easier to add
a common action or set of actions to workers without verbose repetition in each
worker spec.

The key of each member of this object specifies the behavior's name, while the
value may either be a JSON action spec or an array of JSON action specs. See
_action spec_ for more details on actions.

Example:
```JSON
{
  "behaviors": {
    "spinner": { "action": "spin", "duration": "10m" },
    "half load": [
      { "action": "spin", "duration": "5ms" },
      { "action": "sleep", "duration": "5ms" },
    ]
  },
  // ...
}
```

#### workers - Worker Specification

The *workers* member is an array of JSON objects that specify the number,
behavior, and grouping parameters of worker threads making up the workload.

Each array element is a JSON object representing one or more worker threads. A
worker spec may have the following members:

##### name - Worker Name (optional)

The *name* member is an optional string value to associate with each worker
spawned by the containing worker spec. The name is printed in statistics output
and also used as a secondary grouping key in aggregate statistics calculations.

##### group - Worker Group (optional)

The *group* member is an optional string value that is used to group worker
threads during aggregate statistics calculations. The group name is the primary
grouping key.

##### instances - Worker Instances (optional)

The *instances* member is an optinal integer specifying the number of workers to
spawn from the containing worker spec.

The default value is 1.

##### priority - Worker Priority (optional)

The *priority* member is an optional integer or JSON object that specifies the
fair priority or the deadline parameters, respectively, of the workers spawned
from the containing worker spec.

How the value is interpreted depends on whether it is an integer or a JSON
object. When the value is an integer, it is interpreted as a fair priority and
the workers are bound to a fair priority profile. When the value is a JSON
object, it is interpreted as a deadline parameter set and must contain the
string keys "capacity", "deadline", and "period". The values of these keys
are durations representing the corresponding deadline parameters. The workers
are bound to a deadline profile with the given parameters.

Example:
```JSON
// Worker spec with fair priority.
{
  "priority": 24,
  // ...
}

// Worker spec with deadline priority.
{
  "priority": { "capacity": "2ms", "deadline": "10ms", "period": "10ms" },
  // ...
}
```

##### actions - Worker Actions

The *actions* member is an array of JSON objects that describe the sequence of
actions for the worker to perform. In general, actions are performed in the
order specified in the array. When the last action is reached, the worker loops
back to the first action and the sequence is repeated.

The sequence of actions is performed either until the overall workload interval
expires or until an early exit action is executed.

See _action spec_ for more details on actions.

Example:
```JSON
{
  // ...
  "workers": [
    // First worker spec defining a group of 8 identical workers.
    {
      "instances": 8,
      "group": "High Priority",
      "name": "CPU Bound",
      "priority": 24,
      "actions": [
        { "action": "spin", "duration": "10m" },
      ]
    },
    // Second worker spec defining a group of 8 identical workers with different
    // behavior.
    {
      "instances": 8,
      "group": "High Priority",
      "name": "Bursty",
      "priority": 24,
      "actions": [
        { "action": "spin", "uniform": { "min": "1ms", "max": "10ms"} },
        { "action": "sleep", "uniform": { "min": "100ms", "max": "1s"} },
      ]
    },
    // Third worker spec defining a group of 8 identical workers with different
    // priority from the first.
    {
      "instances": 8,
      "group": "Low Priority",
      "name": "CPU Bound",
      "priority": 12,
      "actions": [
        { "action": "spin", "duration": "10m" },
      ]
    },
    // Fourth worker spec defining a group of 8 identical workers with different
    // priority from the second.
    {
      "instances": 8,
      "group": "Low Priority",
      "name": "Bursty",
      "priority": 12,
      "actions": [
        { "action": "spin", "uniform": { "min": "1ms", "max": "10ms"} },
        { "action": "sleep", "uniform": { "min": "100ms", "max": "1s"} },
      ]
    }
  ],
  // ...
}
```

#### tracing - Kernel Tracing Specification (optional)

The *tracing* member is an optional member that provides configuration
parameters for running kernel tracing on the workload. Tracing defaults to off
whenever this member is not present.

##### group mask - Kernel Tracing Group Mask (optional)

The *group mask* member may be an unsigned int or a string. It specifies the
group mask to set up kernel tracing with. If this member is missing, the group
mask defaults to KTRACE_GRP_ALL.

The following groups are supported:
* **KTRACE_GRP_ALL:** 0xFFF
* **KTRACE_GRP_META:** 0x001
* **KTRACE_GRP_LIFECYCLE:** 0x002
* **KTRACE_GRP_SCHEDULER:** 0x004
* **KTRACE_GRP_TASKS:** 0x008
* **KTRACE_GRP_IPC:** 0x010
* **KTRACE_GRP_IRQ:** 0x020
* **KTRACE_GRP_PROBE:** 0x040
* **KTRACE_GRP_ARCH:** 0x080
* **KTRACE_GRP_SYSCALL:** 0x100
* **KTRACE_GRP_VM:** 0x200

##### filepath - Human Readable Kernel Tracing Translation Filepath (optional)

The *filepath* member is a string that indicates where a human readable
translation of the read kernel traces should be saved. If this member is
missing, no human readable translation is generated.

The loadbench package has permission to write to `/tmp` inside its own namespace.
If accessing from the shell, this directory will be located at

```
/tmp/r/sys/fuchsia.com:loadbench:0#meta:loadbench.cmx
```

##### string ref - Kernel Trace String Reference (optional)

The *string ref* member is a string that indicates a string ref to look for
when generating trace stats. If this member is missing, no kernel trace stats
are generated. Similarly, if the provided string ref is not found in the string
ref table or no events are found that match it, no trace stats are generated.

Example:
```JSON
"tracing": {
  "group mask": "KTRACE_GRP_ALL",
  "filepath": "/tmp/latest.ktrace",
  "string ref": "clock_read",
},
```

## Duration Spec

Various objects have duration perameters, incuding intervals, time-based
actions, and deadline profiles. A duration value may either be an integer or
a string containing an integer scalar value and a unit suffix.

When the duration value is an integer, it is taken in units of nanoseconds.

String values must include one of the following units:
* Hours: "h"
* Minutes: "m"
* Seconds: "s"
* Milliseconds: "ms"
* Microseconds: "us"
* Nanoseconds: "ns"

Example:
```JSON

{ "action": "spin", "duration": 1000 } // Spin for 1000ns or 1us.

{ "action": "sleep", "uniform": { "min": "1s", "max": "10s" } } // Sleep for between 1 and 10 s.

{ "priority": { "capacity": "1ms", "deadline": "10ms", "period": "20ms" } }
```

## Action Spec

Actions are JSON objects that describe a discrete action for a worker to
execute. All actions have an *action* member with a string value specifying the
type of action. Actions may require additional members to specify relevant
parameters, such as intervals, objects, and even other actions.

The following actions are supported:

### spin - Spin for an Interval

The *spin* action causes the worker to enter a spin loop for the given interval.
The interval may either be a simple duration or a uniform distribution.

Example:
```JSON
{ "action": "spin", "duration": "2m" }

{ "action": "spin", "uniform": { "min": "1ms", "max": "3ms" } }
```

#### duration

When the spin action contains a *duration* member, the spin will continue for the
constant value given as a _duration spec_.

#### uniform

When the spin action contains a *uniform* member, the spin will continue for the
duration selected from a uniform distrubution. The value must be a JSON object
string keys for the *min* and *max* _duration specs_ defining the uniform range.

### sleep - Sleep for an Interval

The *sleep* action causes the worker to sleep for the given interval. This
action supports the same parameters as the *spin* action.

### write - Write to a Channel

The *write* action causes the worker to write a message with the given number
of bytes to the given named channnel object.

#### channel

The *channel* member specifies the string name of the channel object to write
to.

Example:
```JSON
{ "action": "write", "channel": "main channel", "side": 0, "bytes": 1024 }
```

#### side

The *side* member specifies which side of the channel object to write to. The
value must be an integer with either the value 0 or 1.

#### bytes

The *bytes* member specifies how many bytes to write in the message.

### read - Read from a Channel

The *read* action is similar to the *write* action, except that the given number
of bytes is read from the channel instead of written.

### behavior - Execute a Named Behavior

The *behavior* action causes the worker to execute the given named behavior
from the top-level *behavior* member.

Example:
```JSON
{ "action": "behavior", "name": "spin action" }
```

#### name

The *name* member specifies the string name of the behavior to execute.

### wait_async - Async Wait on an Object

The *wait_async* action causes the worker to perform `zx_object_wait_async` on
the given port and object.

Example:
```JSON
{ "action": "wait_async", "port": "port name", "object": "object name", "signals": 8 }
```

#### port

The *port* member specifies the string name of the port object to register with.
#### object

The *object* member specifies the string name of the object to register with the
port.

#### signals

The *signals* member specifies the integer signal mask of events to wait for.

### wait_one - Wait on an Object

The *wait_one* action causes the worker to perform `zx_object_wait_one` on the
given object.

Example:
```JSON
{ "action": "wait_one", "object": "object name", "signals": 8, "deadline": "5s" }
```

#### object

The *object* member specifies the string name of the object to wait on.

#### signals

The *signals* member specifies the integer signal mask of events to wait for.

#### deadline (optional)

The *deadline* member optionally specifies the timeout duration of the wait
operation.

### port_wait - Wait on a Port

The *port_wait* action causes the worker to perform `zx_port_wait` on the
given port object.

Example:
```JSON
{ "action": "port_wait", "port": "port name", "deadline": "5s" }
```

#### port

The *port* member specifies the string name of the port object to wait on.

#### deadline (optional)

The *deadline* member optionally specifies the timeout duration of the wait
operation.

### signal - Signal an Object

The *signal* action causes the worker to perform `zx_object_signal` on the given
object.

Example:
```JSON
{ "action": "signal", "object": "object name", "clear": 0, "set": 8 }
```

#### object

The *object* member specifies the string name of the object to signal.

#### clear

The *clear* member specifies the integer mask of the events to clear.

#### set

The *set* member specifies the integer mask of the events to set.

### timer_set - Set a Timer Object

The *timer_set* object sets the given timer to the given relative deadline and
optional slack.

Example:
```JSON
{ "action": "timer_set", "timer": "timer name", "deadline": "100ms", "slack": "250us" }
```

#### timer

The *timer* member specifies the string name of the timer object to set.

#### deadline

The *deadline* member specifies the deadline of the timeout, relative to the
current time, as a duration.

#### slack

The *slack* member specifies the timer slack as a duration.

### exit - Exit the Action Loop Early

The *exit* action causes the worker to exit the action loop and idle until the
end of the workload run.

Example:
```JSON
{ "action": "exit" }
```
