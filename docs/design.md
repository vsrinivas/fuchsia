# Fuchsia Tracing Design

This document describes a mechanism for collecting diagnostic trace information
from running applications on the Fuchsia operating system.

## Overview

The purpose of Fuchsia tracing is to provide a means to collect, aggregate,
and visualize diagnostic tracing information from Fuchsia user space
processes and from the Magenta kernel.

## Design Goals

- Lightweight Instrumentation
  - Enabling tracing should not significantly affect the performance of running
    applications.  Trace providers should not need to acquire locks, make
    syscalls, or perform dynamic memory allocation required between the time
    when tracing is activated and when it is disabled.
- Compact Memory Footprint
  - Trace records are stored compactly in memory so that buffers can remain
    small but hold many events.
- Crash-proof
  - It is possible to collect partial traces even if trace providers
    terminate (normally or abnormally) during trace collection.
- Flexible and Universal
  - Can trace code written in any language given a suitable implementation of
    the tracing library.
  - Trace points can be manually inserted by the developer or generated
    dynamically by tools.
- General
  - The trace format defines general purpose record types which support a
    wide range of data collection needs.
  - Trace data can be transformed into other formats for visualization using
    tools such as Catapult or TraceViz.
- Extensible
  - New record types can be added in the future without breaking existing tools.
- Robust
  - Enabling tracing does not compromise the integrity of running components
    or expose them to manipulation by tracing clients.

## Moving Parts

### Trace Manager

The trace manager is a system service which coordinates registration of
trace providers.  It ensures that tracing proceeds in an orderly manner
and isolates components which offer trace providers from trace clients.

The trace manager implements two FIDL interfaces:

- `TraceController`: Provides trace clients with the ability to enumerate
  trace providers and collect trace data.
- `TraceRegistry`: Provides trace providers with the ability to register
  themselves at runtime so that they can be discovered by the tracing system.

TODO: The `TraceRegistry` should be replaced by a `Namespace` based approach
to publish trace providers from components.

### Trace Providers

Components which can be traced or offer tracing information to the system
implement the `TraceProvider` FIDL interface and register it with the
`TraceRegistry`.  Once registered, they will receive messages whenever
tracing is started or stopped and will have the opportunity to provide
trace data encoded in the [Fuchsia Trace Format](trace_format.md).

#### Magenta Trace Provider

The Magenta trace provider is an adapter for Magenta kernel trace events.
It reads trace events from the kernel and publishes them via the Fuchsia
tracing mechanism so that they can be captured alongside trace data from
userspace processes.

### Trace Client

The `trace` program offers command-line access to tracing functionality
for developers.

This information can also be accessed programmatically through the
`TraceController` interface by other tools.

## Trace Libraries

Trace libraries make it easier to create programs which write or read
tracing data.  The following libraries are provided today:

### C++ Trace Event Library

Provides macros and inline functions for instrumenting C++ programs with
trace points for capturing trace data during trace execution.

See `//apps/tracing/lib/trace/event.h`.

#### Example

This example records trace events marking the beginning and end of the
execution of the "DoSomething" function together with its parameters.

```c++
#include "apps/tracing/lib/trace/event.h"

void DoSomething(int a, std::string b) {
  TRACE_DURATION("example", "DoSomething", "a", a, "b", b);

  // Do something
}
```

### C++ Trace Provider Library

Provides functions for registering a trace provider in a C++ program.

See `//apps/tracing/lib/trace/provider.h`.

### C++ Trace Reader Library

Provides functions for reading trace archives.

See `//apps/tracing/lib/trace/reader.h`.

## Transport Protocol

When the developer initiates tracing, the trace manager asks all relevant
trace providers to start tracing and provides each one with a trace buffer
VMO into which they should write their trace records.

While tracing is running, the trace manager continues watching for newly
registered trace providers and activates them if needed.

When tracing finishes, the trace manager asks all of the active trace providers
to stop tracing then waits a short time for them to acknowledge that they
have finished writing out their trace events.

The trace manager then reads and validates trace data written into the trace
buffer VMOs by trace providers and creates a trace archive.  The trace manager
can often recover partial data even when trace providers terminate abnormally
as long as they managed to store some data into their trace buffers.

The trace manager delivers the resulting trace archive to its client through
a socket.  This data is guaranteed to be well-formed according to the
Fuchsia trace format (but it may be nonsensical if trace providers
deliberately emit garbage data).

These are some important invariants of the transport protocol:
- There are no synchronization points between the trace manager and trace
  providers other than starting or stopping collection.
- Trace providers (components being traced) only ever write to trace buffers;
  they never read from them.
- The trace manager only ever reads from trace buffers; it never writes to them.
- Trace clients never see the original trace buffers; they receive trace
  archives over a socket from the trace manager.  This protects trace providers
  from manipulation by trace clients.

## Trace Conversion

The `trace` program supports converting Fuchsia trace archives into other
formats, such as Catapult JSON records.
