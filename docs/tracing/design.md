# Fuchsia Tracing System Design

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

#### Kernel Trace Provider

The `ktrace_provider` program ingests kernel trace events and publishes
trace records.  This allows kernel trace data to be captured and visualized
together with userspace trace data.

### Trace Client

The `trace` program offers command-line access to tracing functionality
for developers.  It also support converting Fuchsia trace archives into
other formats, such as Catapult JSON records which can be visualized
using Catapult (aka. chrome:://tracing).

Trace information can also be collected programmatically by using the
`TraceController` FIDL interface directly.

## Libraries

### libtrace: The C and C++ Trace Event Library

Provides macros and inline functions for instrumenting C and C++ programs
with trace points for capturing trace data during trace execution.

See `<trace/event.h>`.

#### C++ Example

This example records trace events marking the beginning and end of the
execution of the "DoSomething" function together with its parameters.

```c++
#include <trace/event.h>

void DoSomething(int a, std::string b) {
  TRACE_DURATION("example", "DoSomething", "a", a, "b", b);

  // Do something
}
```

#### C Example

This example records trace events marking the beginning and end of the
execution of the "DoSomething" function together with its parameters.

Unlike in C++, it is necessary to specify the type of each trace argument.
In C++ such annotations are supported but are optional since the compiler
can infer the type itself.

```c
#include <trace/event.h>

void DoSomething(int a, const char* b) {
  TRACE_DURATION("example", "DoSomething", "a", TA_INT32(a), "b", TA_STRING(b));

  // Do something
}
```

#### Suppressing Tracing Within a Compilation Unit

To completely suppress tracing within a compilation unit, define the NTRACE
macro prior to including the trace headers.  This causes the macros to
behave as if tracing is always disabled so they will not produce trace
records and they will have zero runtime overhead.

```c
#define NTRACE
#include <trace/event.h>

void DoSomething(void) {
  // This will never produce trace records because the NTRACE macro was
  // defined above.
  TRACE_DURATION("example", "DoSomething");
}
```

### libtrace-provider: Trace Provider Library

This library provides C and C++ functions to register a process's trace
engine with the Fuchsia tracing system.  For tracing to work in your process,
you must initialize the trace provider at some point during its execution
(or implement your own trace handler to register the trace engine some
other way).

The trace provider requires an asynchronous dispatcher to operate.

#### C++ Example

```c++
#include <async/loop.h>
#include <trace-provider/provider.h>

int main(int argc, char** argv) {
  // Create a message loop.
  async::Loop loop;

  // Start a thread for the loop to run on.
  // We could instead use async_loop_run() to run on the current thread.
  mx_status_t status = loop.StartThread();
  if (status != MX_OK) exit(1);

  // Create the trace provider.
  trace::TraceProvider trace_provider(loop.async());

  // Do something...

  // The loop and trace provider will shut down once the scope exits.
  return 0;
}
```

#### C Example

```c
#include <async/loop.h>
#include <trace-provider/provider.h>

int main(int argc, char** argv) {
  mx_status_t status;
  async_t* async;
  trace_provider_t* trace_provider;

  // Create a message loop.
  status = async_loop_create(NULL, &async);
  if (status != MX_OK) exit(1);

  // Start a thread for the loop to run on.
  // We could instead use async_loop_run() to run on the current thread.
  status = async_loop_start_thread(async, "loop", NULL);
  if (status != MX_OK) exit(1);

  // Create the trace provider.
  trace_provider = trace_provider_create(async);
  if (!trace_provider) exit(1);

  // Do something...

  // Tear down.
  trace_provider_destroy(trace_provider);
  async_loop_shutdown(async);
  return 0;
}
```

### libtrace-reader: Trace Reader Library

Provides C++ types and functions for reading trace archives.

See `<trace-reader/reader.h>`.

## Transport Protocol

When the developer initiates tracing, the trace manager asks all relevant
trace providers to start tracing and provides each one with a trace buffer
VMO into which they should write their trace records.

While a trace is running, the trace manager continues watching for newly
registered trace providers and activates them if needed.

If a trace provider's trace buffer becomes full while a trace is running,
that trace provider will stop recording events but other trace providers will
continue to record trace events into their own buffers as usual until the
trace stops as usual.  This may result in a partially incomplete trace.

TODO(MG-1107): Improve buffering behavior to support continuous tracing.

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
