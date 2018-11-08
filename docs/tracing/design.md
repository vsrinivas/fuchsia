# Fuchsia Tracing System Design

This document describes a mechanism for collecting diagnostic trace information
from running applications on the Fuchsia operating system.

## Overview

The purpose of Fuchsia tracing is to provide a means to collect, aggregate,
and visualize diagnostic tracing information from Fuchsia user space
processes and from the Zircon kernel.

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
for developers.  It also supports converting Fuchsia trace archives into
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
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

int main(int argc, char** argv) {
  // Create a message loop.
   async::Loop loop(&kAsyncLoopConfigNoAttachToThread);

  // Start a thread for the loop to run on.
  // We could instead use async_loop_run() to run on the current thread.
  zx_status_t status = loop.StartThread();
  if (status != ZX_OK) exit(1);

  // Create the trace provider.
  trace::TraceProvider trace_provider(loop.dispatcher());

  // Do something...

  // The loop and trace provider will shut down once the scope exits.
  return 0;
}
```

#### C Example

```c
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>

int main(int argc, char** argv) {
  zx_status_t status;
  async_loop_t* loop;
  trace_provider_t* trace_provider;

  // Create a message loop.
  status = async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop);
  if (status != ZX_OK) exit(1);

  // Start a thread for the loop to run on.
  // We could instead use async_loop_run() to run on the current thread.
  status = async_loop_start_thread(loop, "loop", NULL);
  if (status != ZX_OK) exit(1);

  // Create the trace provider.
  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
  trace_provider = trace_provider_create(dispatcher);
  if (!trace_provider) exit(1);

  // Do something...

  // Tear down.
  trace_provider_destroy(trace_provider);
  async_loop_shutdown(loop);
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

What happens when a trace provider's trace buffer becomes full while a trace
is running depends on the buffering mode.
See [Buffering Modes](#Buffering Modes) below.

When tracing finishes, the trace manager asks all of the active trace providers
to stop tracing then waits a short time for them to acknowledge that they
have finished writing out their trace events.

The trace manager then reads and validates trace data written into the trace
buffer VMOs by trace providers and creates a trace archive.  The trace manager
can often recover partial data even when trace providers terminate abnormally
as long as they managed to store some data into their trace buffers.
Note that in streaming mode the trace manager only needs to save the
currently active rolling buffer.
See [Buffering Modes](#Buffering Modes) below.

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

## Buffering Modes

There are three buffering modes: oneshot, circular, and streaming.
They specify different behaviors when the trace buffer fills.

Note that in all cases trace provider behavior is independent of each other.
Other trace providers can continue to record trace events into their own
buffers as usual until the trace stops, even as one provider's buffer fills.
This may result in a partially incomplete trace.

### Oneshot

If the buffer becomes full then that trace provider will stop recording events.

### Circular

The trace buffer is effectively divided into three pieces: the "durable" buffer
and two "rolling" buffers. The durable buffer is for records important enough
that we don't want to risk dropping them. These include records for thread and
string references.

Tracing begins by writing to the first rolling buffer. Once one rolling buffer
fills tracing continues by writing to the other one.

If the durable buffer fills then tracing for the provider stops. Tracing in
other providers continues as usual.

### Streaming

The trace buffer is effectively divided into three pieces: the "durable" buffer
and two "rolling" buffers. The durable buffer is for records important enough
that we don't want to risk dropping them. These include records for thread and
string references.

Tracing begins by writing to the first rolling buffer. Once one rolling buffer
fills tracing continues by writing to the other one, if it is available, and
notifying the trace manager that the buffer is full. If the other rolling
buffer is not available, then records are dropped until it becomes available.
The other rolling buffer is unavailable between the point when it filled and
when the manager reports back that the buffer's contents have been saved.

Whether records get dropped depends on the rate at which records are created
vs the rate at which the trace manager can save the buffers. This can result
in a partially incomplete trace, but is less important than perturbing program
performance by waiting for a buffer to be saved.

If the durable buffer fills then tracing for the provider stops. Tracing in
other providers continues as usual.

## Trace Manager/Provider FIFO Protocol

Notification of trace provider startup and shutdown is done via a FIFO,
the handle of which is passed from the trace manager to each trace provider
as part of the initial "start tracing" request. The form of each message is
defined in `<trace-provider/provider.h>`. Packets are fixed size with the
following format:

```cpp
typedef struct trace_provider_packet {
    // One of TRACE_PROVIDER_*.
    uint16_t request;

    // For alignment and future concerns, must be zero.
    uint16_t reserved;

    // Optional data for the request.
    // The contents depend on the request.
    // If unused they must be passed as zero.
    uint32_t data32;
    uint64_t data64;
} trace_provider_packet_t;
```

### FIFO Packets

The following packets are defined:

**TRACE_PROVIDER_STARTED**

Sent from trace providers to the trace manager.
Notify the trace manager that the provider has received the "start tracing"
request and is starting to collect trace data.
The `data32` field of the packet contains the version number of the FIFO
protocol that the provider is using. The value is specified by
**TRACE_PROVIDER_FIFO_PROTOCOL_VERSION** in `<trace-provider/provider.h>`.
If the trace manager sees a protocol it doesn't understand it will close
its side of the FIFO and ignore all trace data from the provider.

**TRACE_PROVIDER_SAVE_BUFFER**

Sent from trace providers to the trace manager in streaming mode.
Notify the trace manager that a buffer is full and needs saving.
This request is only used in streaming mode.
The `data32` field contains the "wrap count" which is the number of times
writing has switched from one buffer to the next. The buffer that needs saving
is `(data32 & 1)`.
The `data64` field contains the offset of the end of data written to the
"durable" buffer.

Only one buffer save request may be sent at a time. The next one cannot be
sent until **TRACE_PROVIDER_BUFFER_SAVED** is received acknowledging the
previous request.

**TRACE_PROVIDER_BUFFER_SAVED**

Sent from the trace manager to trace providers in streaming mode.
Notify the trace provider that the requested buffer has been saved.
The `data32` and `data64` fields must have the same values from the
originating **TRACE_PROVIDER_SAVE_BUFFER** request.
