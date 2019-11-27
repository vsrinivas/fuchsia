# Tracing tutorial

A tutorial for enabling tracing in your code.

## Overview

Tracing is used for a number of reasons, including:

* to log unusual conditions,
* to debug software,
* to perform measurements,
* to produce a timeline of events, and
* to keep track of statistics.

## Topics

In this tutorial, we'll examine tracing in Fuchsia and address the following
topics:

- What is tracing?
- Why do I want to use it?
- What's the absolute simplest way to do a tracing "hello world"?
- What are the "best practices" for tracing?
- How do I correlate trace data from disparate sources?

# Concepts

In the Fuchsia tracing system, three types of
[components](/docs/glossary.md#component) cooperate in a distributed manner:

* Trace Manager &mdash; an administrator that manages the overall tracing system
* Trace Provider &mdash; a program that generates trace data
* Trace Client &mdash; a program that consumes trace data

Let's suppose your program wants to create trace data.
Your program would play the role of a trace provider &mdash; it generates
tracing data.
There can, of course, be many trace providers in a system.

Let's further suppose that a different program wants to process the
data that your trace provider is generating.
This program takes on the role of a trace client, and like the trace provider,
there can be many trace clients in a system.

> Note that there's currently just one trace client, with no immediate plans
> to add more. We'll discuss this further below, in the
> [Trace Client operation](#trace-client-operation) section.

What's interesting about Fuchsia's distributed implementation is that
for efficiency, the trace provider writes the data directly into a shared
memory segment (a Zircon [**VMO** &mdash; Virtual Memory Object]
(/docs/zircon/objects/vm_object.md)).
The data isn't copied anywhere, it's stored in memory as it's generated.

This means that the trace client must somehow find out where that data is
stored. This discovery is mediated by the trace manager.

There's exactly one trace manager in the system, and it serves as a central
rendezvous point: a place where trace providers and trace clients meet.

## Walkthrough

Let's look at this one step at a time.

### Trace Provider operation

Your program starts a background async loop (implemented as a thread in
C, for example).
This allows it to play the role of a trace provider by handling messages from
the trace manager (like "start tracing" or "stop tracing").

Then, your program registers with the trace manager, telling it that it is
ready to take on the role of being a trace provider.

Finally, your program goes about its business.
Note that no tracing is happening yet &mdash; we're waiting for the trace
manager to start tracing in your program.

When tracing starts, the trace manager provides your program with a VMO into
which it can write its data.

From the perspective of your program, there's nothing special you need to do
to handle the interaction with the async loop (that is, to turn tracing on or
off) &mdash; your program calls the various tracing API functions, and they
themselves determine if data should be written to the VMO or not.

> Note that tracing can be disabled entirely at compile-time.
> In this case, *no* tracing code is present in your program;
> and thus there's no way to turn it on.
> A program built with tracing *enabled*, however, can respond to commands from
> the trace manager to selectively enable or disable tracing.

### Trace Client operation

When a program wishes to assume the role of a trace client (that is, to get
trace data), it contacts the trace manager, requests tracing to start (and
subsequently stop), and then finally saves collected trace data.
The trace manager gathers the data and sends it over a socket to the trace
client.

### Decoupling

Because the trace provider writes to the VMO, the trace manager reads from
the VMO, and the trace client reads data from a socket (provided by the trace
manager), there's no way for the trace client to directly affect the operation
of the trace provider.

## On Demand

Tracing is on-demand &mdash; that is, your program normally runs with tracing
turned off.
When some event occurs (e.g., a system problem, or the user initiates a
debugging session), tracing can be turned on for an arbitrary period.
Not only can tracing be turned on or off, but specific categories of tracing
can be individually selected (we'll see this shortly).

Some time later, tracing can be turned off again.

## `trace` and `traceutil`

As mentioned above, there is currently just the one trace client.
It consists of two utilities: `trace` and `traceutil`.
The `trace` utility runs on the target, and `traceutil` runs on the development
host.

`trace` is used to control tracing &mdash; it sends the commands to the trace
manager to start and stop tracing, and it gathers the trace data.

`traceutil`, on the development host side, communicates with `trace`.
Trace data can be streamed from `trace` through `traceutil` right to the
developer's desktop.

# Hello World

Assuming that the background async loop is started and running (see
[Fuchsia Tracing System Design](design.md) for details),
this is the minimum code you need in order to write a simple string to
the trace buffer:

```c
TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS, "message", TA_STRING("Hello, World!"));
```

There are 5 arguments to the macro `TRACE_INSTANT()`.
In order, they are:

1. `"category"` &mdash; this is a nul-terminated string representing the
   category of the trace event.
2. `"name"` &mdash; a nul-terminated string representing the name of the trace
   event.
3. `TRACE_SCOPE_PROCESS` &mdash; for the `TRACE_INSTANT` tracing macro, this
   indicates the scope of the event.
4. `"message"` &mdash; this is the "key" part of the data.
5. `TA_STRING("Hello, World!")` &mdash; this is the "value" part of the data.

The result of executing this code is that if tracing is compiled in, and
enabled, a trace datum will be logged to the VMO.
If tracing is compiled in, but not enabled, this code returns almost
immediately (it checks to see if tracing is enabled, and discovering that it's
not, doesn't do anything else).
If tracing isn't compiled in, this code doesn't even make it past the C
compiler &mdash; no code is generated (it's like the entire `TRACE_INSTANT()`
macro was a comment).

> The key and value arguments are optional, and can be repeated.
> Whenever you specify a key you must specify a value (even if the value
> is nul).

## Category

What is a category?

A category is something that you define; there's a convention for how
categories should look:

*provider*`:`*category*[`:`*sub-category*[...]]

For example, "`demo:flow:outline`" which has three colon-delimited elements:

* `demo` is the name of the trace provider; it identifies your program
* `flow` is the name of the category; here, we're using a name that suggests
  that we are tracing the call-by-call flow of the program
* `outline` is the name of the sub-category; here, we're using a name that
  suggests that we are tracing the high-level flow of the program, perhaps
  just a few top-level functions.

We might have another category name, `demo:flow:detailed`, for example, which
we could use to trace the detailed flow of the program.

> The names are at your discretion; whatever has meaning for you.
> Beware though, that the category namespace is global to all programs running,
> so if there was another program with the "provider" set to `demo` as well,
> you would most likely run into naming conflicts (and thus could end up with
> unrelated data from some other trace provider).

Categories should be grouped hierarchically.
For example, with statistics collection, you might have some sub-categories:

* `demo:statistics:bandwidth` &mdash; to collect bandwidth statistics,
* `demo:statistics:requests` &mdash; to collect user request statistics.

Categories, therefore, give you control over what's collected.
If a category is not requested by the trace client program, then the data is
not collected by the trace provider.

## Name

The name argument (2nd argument in our `TRACE_INSTANT()`, and in fact most
other macros) is a string description of the event data.

So, if we had a category of `demo:statistics:bandwidth` we might have two
different trace points, one that counted received packets and another that
counted transmitted packets.
We'd use the name to distinguish them:

```c
// we just received another packet, log it
status.rx_count++;
TRACE_INSTANT("demo:statistics:bandwidth", "rxpackets", TRACE_SCOPE_PROCESS,
              "count", TA_UINT64(status.rx_count));
```

versus

```c
// we just transmitted another packet, log it
status.tx_count++;
TRACE_INSTANT("demo:statistics:bandwidth", "txpackets", TRACE_SCOPE_PROCESS,
              "count", TA_UINT64(status.tx_count));
```

> Notice that, as mentioned above, we have both a key (the `"count"` argument)
> and a value (the `TA_UINT64()` encoded value).

## Encoding

In the examples above, we saw `TA_STRING()` and `TA_UINT64()` macros.
They're used to properly encode the value for C.
For C++, you can still use the macro, but it's not
required because the value type can be inferred (for all but three, shown in
the table below as "req'd" in the "C++" column).

Macro             |  C++  | Description
------------------|-------|--------------------------------------------------------------
TA_NULL           |       | a null value.
TA_BOOL           |       | a boolean value.
TA_INT32          |       | a signed 32-bit integer value.
TA_UINT32         |       | an unsigned 32-bit integer value.
TA_INT64          |       | a signed 64-bit integer value.
TA_UINT64         |       | an unsigned 64-bit integer value.
TA_DOUBLE         |       | a double-precision floating point value.
TA_CHAR_ARRAY     | req'd | a character array with a length (copied rather than cached).
TA_STRING         |       | a null-terminated dynamic string (copied rather than cached).
TA_STRING_LITERAL | req'd | a null-terminated static string constant (cached).
TA_POINTER        |       | a pointer value (records the memory address, not the target).
TA_KOID           | req'd | a kernel object id.

For the most part, the above operate as you'd expect.
For example, the `TA_INT32()` macro takes a 32-bit signed integer as an
argument.

The notable exceptions are:

* `TA_NULL()` &mdash; does not take an argument, that is, it's written
  literally as `TA_NULL()` (in C++, you could use `nullptr` instead of the
  `TA_NULL()` macro).
* `TA_CHAR_ARRAY()` &mdash; takes two arguments, the first is a pointer to the
  character array, and the second is its length.

### C++ notes

Note that in C++, when using a literal constant, type inference needs a hint
in order to get the size, signedness, and type right.

For example, is the value `77` a signed 32-bit integer? An unsigned 32-bit
integer? Or maybe even a 64-bit integer of some kind?

Type inference in the tracing macros works according to the standard C++ rules:

*   `77` is a signed 32-bit integer, `TA_INT32`
*   `77U` is an unsigned 32-bit integer, `TA_UINT32`
*   `77L` is a signed 64-bit integer, `TA_INT64`
*   `77LU` is an unsigned 64-bit integer, `TA_UINT64`

This also means that floating point needs to be explicitly noted if it's an
(otherwise) integer value.
`77` is, as above, a `TA_INT32`, but `77.` (note the period) is a `TA_DOUBLE`.

For this reason, if you're using constants, you should consider retaining the
encoding macros if you're expressing the values directly, or you should use
the appropriate `const` type:

```cpp
TRACE_INSTANT("category", "name", "int", 77);   // discouraged
```

is the same as:

```cpp
const int32_t my_id = 77;                       // well defined type
TRACE_INSTANT("category", "name", "int", my_id);
```

and:

```cpp
#define MY_ID   (TA_INT32(77))                  // uses the typing macro
TRACE_INSTANT("category", "name", "int", MY_ID);
```

## Multiple key/value pairs

As mentioned above, you can have zero or more key/value pairs.
For example:

```cpp
TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS);
TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS, "key1", nullptr);
TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS, "key1", "string1");
TRACE_INSTANT("category", "name", TRACE_SCOPE_PROCESS, "key1", "string1", "key2", 77);
```

## Scope

For the `TRACE_INSTANT()` macros, there are three values of the "scope"
(3rd argument):

Scope               | Meaning
--------------------|---------
TRACE_SCOPE_THREAD  | The event is only relevant to the thread it occurred on
TRACE_SCOPE_PROCESS | The event is only relevant to the process in which it occurred
TRACE_SCOPE_GLOBAL  | The event is globally relevant

# Conditional compilation for tracing

There are cases where you might wish to entirely disable tracing (like final
release).

The `NTRACE` macro is what's used to make this happen.

This is similar to the `NDEBUG` macro used with **assert()** &mdash; if the
macro is present, then the **assert()** calls don't generate any code.

In the case of tracing, if the `NTRACE` macro is present, then the tracing
macros don't generate any code.

> In particular, keep in mind the *negative* sense &mdash; if the macro is
> **present**, then tracing is **disabled**.

You can explicitly turn on the macro yourself to disable tracing:

```c
#define NTRACE  // disable tracing
#include <trace/event.h>
```

Here, the macros contained in the tracing file (like `TRACE_INSTANT()`) are
made inactive; they're effectively converted to comments, which are eliminated
by the compiler.

> Notice that we defined the macro *before* the `#include` &mdash; this is
> required in order to select the inactive forms of the macro expansions in
> the `#include` file.

You can also test the `NTRACE` macro, to see if you need to provide tracing
data.

In the example above, where we discussed the `rxpackets` and `txpackets`
counters, you might have a general statistics structure:

```c
typedef struct {
#ifndef NTRACE  // reads as "if tracing is not disabled"
    uint64_t    rx_count;
    uint64_t    tx_count;
#endif
    uint64_t    npackets;
} my_statistics_t;
```

The `rx_count` and `tx_count` fields are used only with tracing, so if `NTRACE`
is asserted (meaning tracing is completely disabled), they don't take up any
room in your `my_statistics_t` structure.

This does mean that you need to conditionally compile the code for managing
the recording of those statistics:

```c
#ifndef NTRACE
    status.tx_count++;
    TRACE_INSTANT("demo:statistics:bandwidth", "txpackets", TRACE_SCOPE_PROCESS,
                  "count", TA_UINT64(status.tx_count));
#endif  // NTRACE
```

## Determining if tracing is on or off

There's one more case to consider.

Sometimes, you may wish to determine if tracing is on at runtime.
There's a handy test macro, `TRACE_ENABLED()`.
If tracing is compiled in (`NTRACE` is not defined), then the `TRACE_ENABLED()`
macro looks to see if tracing is currently turned on or off in your trace
provider, and returns a true or false value at runtime.
Note that if tracing is compiled out, then `TRACE_ENABLED()` always returns
false (generally causing the compiler to entirely optimize out the code).

For example:

```c
#ifndef NTRACE
    if (TRACE_ENABLED()) {
        int v = do_something_expensive();
        TRACE_INSTANT(...
    }
#endif  // NTRACE
```

Here, if tracing is compiled in, **and** enabled, we call
**do_something_expensive()**, perhaps to fetch some data for tracing.

Notice that we used both the `#ifndef` and the `TRACE_ENABLED()` macro
together.
That's because the function **do_something_expensive()** might not exist in the
trace-disabled version, and thus you'd get compiler and linker diagnostics.

### Category selection

There's a similar macro, `TRACE_CATEGORY_ENABLED()` that simply gives you more
refinement; you can test if a particular category is enabled or not.
As with `TRACE_ENABLED()`, if tracing is compiled out, this macro reduces to an
`if (0)`, which the compiler optimizes out.

# Timing events

Another common function during tracing is timing things.
Often, you'll want to know "how long does this operation take?"
or "how long does this procedure run for?"

There are three macros that can be used here:

* `TRACE_DURATION()` &mdash; monitor the duration of the current scope,
* `TRACE_DURATION_BEGIN()` and `TRACE_DURATION_END()` &mdash; monitor the
  duration of a specific, bounded section of code.

> These timing macros are often used without any key/value data; we'll
> show them both ways below.

For example:

```c
int my_function(void) {
    TRACE_DURATION("demo:timing:functions", "my_function");
    // your function does stuff here...
}
```

This generates a trace event when **my_function()** ends, indicating the length
of time spent in the function (and all called functions).

> Yes, this works in C &mdash; a compiler extension is used to add a code hook
> at scope end.

## Use during constructor

A fairly common use case for the `TRACE_DURATION()` macro is in C++
constructors (and other member functions), with additional data captured at
the same time.

This is from one of the `blobfs` vnode constructors
(`zircon/system/ulib/blobfs/blobfs.cpp`):


```cpp
zx_status_t VnodeBlob::InitCompressed() {
    TRACE_DURATION("blobfs", "Blobfs::InitCompressed", "size", inode_.blob_size,
                   "blocks", inode_.num_blocks);
    ...
```

Here, the length of time spent in the constructor, along with the size and
number of blocks, is captured.
By the way, notice how the macros for the types of `inode_.blob_size` and
`inode_.num_blocks` are not used in the C++ version &mdash; their type is
inferred by the compiler.

## Use for arbitrary scope

`TRACE_DURATION()` can be used for any scope, not just an entire function:

```c
int my_function(void) {
    if (this) {
        TRACE_DURATION("demo:timing:functions", "my_function", "path", TA_STRING ("this"));
        // do stuff that's timed
    } else {
        TRACE_DURATION("demo:timing:functions", "my_function", "path", TA_STRING ("that"));
        // do other stuff that's timed
    }
}
```

Here, two different timing durations are captured, depending on which path is
taken in the code, with the key/value pair indicating the selected one.

For greater control over the area that's timed, you can use the
`TRACE_DURATION_BEGIN()` and `TRACE_DURATION_END()` macros:

```c
int my_function(void) {
    if (this) {

        // do something that you don't want to time here

        // start timing
        TRACE_DURATION_BEGIN("demo:timing:functions", "my_function:area1");

        // do something that you'd like to time here

        // end timing
        TRACE_DURATION_END("demo:timing:functions", "my_function:area1");

        // do something else that you don't want to time here
    }
}
```

In this sample, we added some path information to the name component.

The rule here is that the `TRACE_DURATION_BEGIN()` must have a matching
`TRACE_DURATION_END()` macro in the same scope.
Matching means that both the category and name must be the same.

> The macros must can be nested hierarchically.

For example:

```c
int my_function(void) {
    if (this) {

        // do something that you don't want to time

        TRACE_DURATION_BEGIN("demo:timing:functions", "my_function:area1");
        // NOTE 1

        // do something that you'd like to time

        TRACE_DURATION_BEGIN("demo:timing:functions", "my_function:inner");
        // NOTE 2

        // do something that you'd like timed by "inner"

        // NOTE 3
        TRACE_DURATION_END("demo:timing:functions", "my_function:inner");

        // do something that's still timed by "area1" but not "inner"

        // NOTE 4
        TRACE_DURATION_END("demo:timing:functions", "my_function:area1");

        // do something else that you don't want to time
    }
}
```

To be clear about what the above is doing &mdash; there are two parts of the
code being timed; an overall "my_function:area1" that spans from "NOTE 1"
through to and including "NOTE 4", and a separately timed area
"my_function:inner" that spans from "NOTE 2" through to and including "NOTE 3".

> Tip: prefer the **TRACE_DURATION()** macro over the
> **TRACE_DURATION_BEGIN()** and **TRACE_DURATION_END()** macros. The simple
> **TRACE_DURATION()** macro automatically handles leaving scope, whereas the
> begin/end style macros don't &mdash; you need to manually ensure correct
> nesting and termination.
>
> Also, note that **TRACE_DURATION()** takes roughly *half* the space in the
> output buffer as **TRACE_DURATION_BEGIN()** and **TRACE_DURATION_END()** do!
> This can have a big impact on size.

## Resolution

All trace timing is expressed as an unsigned 64-bit count of 1 nanosecond
ticks, giving a 584+ year range (which should be sufficient for all but the
most patient of users).

# Asynchronous tracing

All of the examples so far have been "synchronous" &mdash; that is, occurring
in a linear fashion in one thread.

There's a set of "asynchronous" tracing functions that are used when the
operation spans multiple threads.

For example, in a multi-threaded server, a request is handled by one thread,
and then put back on a queue while the operation is in progress.
Some time later, another thread receives notification that the operation has
completed, and "picks up" the processing of that request.
The goal of asynchronous tracing is to allow the correlation of these disjoint
trace events.

Asynchronous tracing takes into consideration that the same code path is used
for multiple different flows of processing.
In the previous examples, we were interested in seeing how long a particular
function ran, or what a certain value was at a given point in time.
With asynchronous tracing, we're interested in tracking the same data, but for
a logical processing flow, rather than a program location based flow.

In the queue processing example, the code that receives requests would tag each
request with a "nonce" &mdash; a unique value that follows the request around.
This nonce can be generated via `TRACE_NONCE()`, which simply increments a
global counter.

Let's see how this works.
First, you declare a place to hold the nonce.
This is usually in a context structure for the request itself:

```c
typedef struct {
...
    // add the nonce to your context structure
    trace_async_id_t async_id;
} my_request_context_t;
```

When the request arrives, you fetch a nonce and begin the asynchronous tracing
flow:

```c
// a new request; start asynchronous tracing
ctx->async_id = TRACE_NONCE();
TRACE_ASYNC_BEGIN("category", "name", ctx->async_id, "key", TA_STRING("value"));
```

You can log trace events periodically using the `TRACE_ASYNC_INSTANT()` macro
(similar to what we did with the `TRACE_INSTANT()` macro above):

```c
TRACE_ASYNC_INSTANT("category", "name", ctx->async_id, "state", TA_STRING("phase2"));
```

And clean up via `TRACE_ASYNC_END()`:

```c
TRACE_ASYNC_END("category", "name", ctx->async_id);
```

> Don't confuse this use of "async" with the async loop that's running in your
> process; they aren't related.

# Flow tracing

Asynchronous tracing is intended for tracing within the same process, but
perhaps by way of different threads.

There's a higher-level tracing mechanism, called "flow" tracing, that's
intended for use between processes or abstraction layers.

You call `TRACE_FLOW_BEGIN()` to mark the start of a "flow".
Just like `TRACE_ASYNC_BEGIN()`, you pass in a nonce to identify this
particular flow. The flow ID is an unsigned 64-bit integer.

Then, you (optionally) call `TRACE_FLOW_STEP()` to indicate
trace operations within that flow.

When you're done, you end the flow with `TRACE_FLOW_END()`.

A flow could be used, for example, between a client and server for tracking a
request end-to-end from the client, through the server, and back to the client.

# Provider registration

Trace providers must register with Trace Manager in order for them to
participate in tracing. This registration involves two pieces:

- code to do the registration,
- an entry in the component manifest to give the component access
  to Trace Manager.

## Registration

The simple form of registration in C++ requires an async loop.

Here's a simple example:

```cpp
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <trace-provider/provider.h>
// further includes

int main(int argc, const char** argv) {
  // process argv

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TracelinkProviderWithFdio trace_provider(
      loop.dispatcher(), "my_trace_provider");

  // further setup

  loop.Run();
  return 0;
}
```

This example uses `fdio` to set up the FIDL channel with Trace Manager.

## Component access

Like all things in Fuchsia, access to capabilities must be spelled out,
there is no ambient authority. Trace providers indicate their need to
communicate with Trace Manager by saying so in their component manifest,
which typically lives in a file with a `.cmx' suffix.

Here's a simple example:

```json
{
    "program": {
        "binary": "bin/app"
    },
    "sandbox": {
        "services": [
            "fuchsia.tracing.provider.Registry"
        ]
    }
}
```

For further information on component manifests, see
[Component Manifests](/docs/concepts/storage/component_manifest.md).

# Background

> @@@ Here we'll highlight the differences and similarities amongst logging,
> tracing, inspection, and debugging.

# References

* [Adding Tracing to Device Drivers](/docs/concepts/drivers/tracing.md)
  gives details on source code additions (e.g., what `#include` files to add)
  and Makefile additions required by the trace provider in order to add
  tracing, or disable it completely.
* [Fuchsia Tracing System Design](design.md)
  goes through the design goals of the tracing system.
* [Fuchsia Trace Format](trace-format/README.md)
  is a reference for the in-memory data format used by the tracing system.
