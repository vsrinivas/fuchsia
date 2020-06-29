# System Monitor Harvester

The Harvester runs on the Fuchsia device, acquiring Samples (units of
introspection data) that it sends to the Host using the Transport system.

The Harvester should not unduly impact the Fuchsia device being monitored.
So the Harvester does not store samples. Instead the samples are moved to
the Dockyard as soon as reasonable.

## Running tests

Before running the tests, add `//bundles:tests` to your `fx set` (i.e. args.gn),
then execute:
```
$ fx test system_monitor_harvester_tests
```
If the test is not already built, `fx test` will build it before running it.

## Code and Concepts

#### Audience

If you are working on the Harvester code, this information may be of help to
you. If you're only interested in using the Harvester, feel free to skip this
section.

#### Overview

The Harvester has a handful of moving pieces and a couple sample gathering
threads.

### Dockyard Proxies

While there are several Dockyard Proxies defined, only one is used at any given
time.

*Fake:* The fake is used only within tests.

*gRPC:* The gRPC Dockyard Proxy is used to send Samples to the Dockyard. It's
    the 'real version' that is used when running Fuchsia DevTools.

*local:* This proxy is like a local loopback or stub. It doesn't actually send
    any messages to the Dockyard. It is activated by passing the `--local`
    option on the command line when starting the Harvester. The primary use
    is for interactive development of the Harvester or in e2e tests.

### Gatherers

The 'gather_category.h/cc' contains a base class for the other gather_* files.
Each of the other Gather* classes derive from GatherCategory and will collect
samples of a particular type.

*cpu:* gathers global, system-wide samples of the CPU usage. This doesn't
    include per-thread usage, for example.

*inspectable:* gathers a list of inspectable components (in a Component
    Framework Inspect sense). This list can be used for further drilling down
    with introspection.

*introspection:* is not actually implemented at this time. The intent is to use
    the Component Framework inspection lib to get component details. When this
    task is revived the inspection lib will be called by this code.

*memory:* gathers global, system-wide samples of the memory usage. This doesn't
    include per-process usage, for example.

*memory_digest:* gathers memory usage bucketed into predefined (hard-coded)
    groups. This code makes use of memory digest lib. It is conceptually like
    a filter/groups on the processes_an _memory gatherer.

*processes_and_memory:* gathers a list of active processes and the memory used
    by each process. Tip: processes don't use CPU directly, when we talk about
    the CPU used by a process we're really talking about the summation of the
    CPU used by child threads of the process.

*tasks:* is a predecessor to processes_and_memory and threads_and_cpu. It's old
    and should probably be removed. This will gather all the job/process/thread
    information (memory and CPU too) in one go. The trouble is that this is not
    fast and it's not efficient. It was simply a first MVP version.

*threads_and_cpu:* gathers a list of active thread and the CPU used
    by each thread.

Of this collection of gatherers, only processes_and_memory and threads_and_cpu
are active beyond startup. The gather_cpu, gather_memory, and gather_tasks are
used at Harvester startup (only) to get a snapshot of unchanging values (like
the total physical memory).

The gather_inspectable, gather_introspection, and gather_memory_digest could be
revived and made useful.

#### Device vs. Global vs. Task Samples

Samples come in three broad flavors.

*Device* samples do not change during a given session. An example is physical
memory. The device's main RAM is not something that can be modified on the fly.
It is set at system startup and remains constant.

*Global* samples are not related to individual jobs, processes, and threads.
The global CPU and memory is *not* determined by adding up all the values used
by the tasks. Instead these values come directly from the kernel (from
accumulators tracked for the purpose of this kind of reporting). This also
implies that the values tracked by global samples and task samples may disagree
- these disagreements are intractable to eliminate. It's best to just be aware
of them (rather than fight the discrepancies).

*Task* samples are gathered by walking the task tree (described below). These
values change frequently since they include all the jobs, processes, and
threads.

### Harvester Class

The Harvester class is a singleton that hold the active dockyard proxy and kicks
off the gatherers. It's purpose is largely to keep main() cleaner.

### Samples

Samples are the pieces of data that the Harvester sends to the Dockyard. In
essence, gathering and sending the samples is the purpose of the Harvester.
To get a better understanding of what a Sample is, refer to the Dockyard docs
and code; and the protobuf definitions used by the Harvester to send Samples
to the Dockyard (these are defined in the Dockyard code).

The reason Samples are mentioned in this document is to convey a sense of why
the Harvester component exists. To drive the point further, if some other code
would create protobufs and send samples to the Dockyard over gRPC the Harvester
would be unnecessary.

### Task Tree

The generic name for jobs, processes, and threads used by the Fuchsia kernel is
*task*. The tasks are arranged in a Directed Acyclic Graph (DAG), i.e. a tree.
The root will always be a job, while the leaves will be threads.

This tree of tasks is an important concept for a couple of reasons:
- It is used by gathers that get per-process and per-thread data.
- It is cached (and re-used)*
- It is expensive to generate*

*That may change at some point. It can be made inexpensive and if that were so,
the advantage of caching it would fade.

There are two cached task trees because each of the two threads maintains its
own. This avoids thread cross-talk/dependencies.

### Kernel Concepts

We're not going to discuss the entire kernel here, but there are some concepts
that are valuable for the Harvester developer to understand.

#### KOID

The Kernel Object Identifier (koid) is a 64 bit integer that uniquely IDs a
'thing' in the kernel. The 'thing' could be *many* things. In the Harvester the
common things with a koid are:
  - each job has a koid
  - each process has a koid (there's even one for the Harvester itself)
  - each thread has a koid

These ID pools are not separate. I.e. a given thread koid will not be equal to a
process koid (or job koid, or anything else). Within the scope of that kernel,
that will be the only 'thing' with that koid.

The koids have no relevance between executions. I.e. if a process had koid of
345678 and we reboot the OS - it's *very* unlikely that the same program will
have the same koid when it runs again. For example, to compare
the memory usage of a program today vs. yesterday the koid for each run would
need to be determined (there's no value in comparing yesterday's koid:2345 with
today's koid:2345 - that koid might refer to an entirely different 'thing'
today).

#### Object Properties

Many of the sample gatherers make requests of 'object' info or properties. This
is related to the koid topic above. The kernel thinks of all the jobs,
processes, and threads as 'objects' and has a generic interface to get
information about these kernel objects.

### Threads

There are two threads that gather sample data in the Harvester. (There are
other threads are created by the gRPC transport layer, but those are managed by
gRPC so we will ignore them here).

The threads have the informative names of "fast" and "slow". There's nothing
particularly fast or slow about the threads themselves. The names come from the
gatherers that run on each. If a gatherer does not block for long periods and
should run frequently it can be run in the fast thread. If a gatherer may block
for long periods it should go on the slow thread (specifically so that it
doesn't block execution on the fast thread). So the names can be thought of as,
"the code that runs on this thread is <name>."

The threads are managed in harvester_main.cc. One of them is actually the main
thread repurposed. In C++ the main thread is not special (like it is in Python,
for example).

Each thread runs an `async::Loop`, using a `dispatcher` to execute work. We use
this system to schedule a callback to run. We then reschedule the same function
after each call (creating a chain). This is similar to having a while loop with
a `sleep` in it, but using `async::Loop` allows the Harvester to behave as other
Fuchsia components do. See the `async::Loop` docs for more information.

### Testing

Many of the code files have an accompanying *_test.cc file. Tests are written
using gTest. Files missing *_test.cc are likely an oversight and should have
tests added.

#### dockyard_host

The dockyard_host is a host-side command line program. The program wraps the
dockyard library with enough to start up the library, wait for the harvester to
connect, and do some test queries. This is a great benefit for someone coding
on the Harvester or Dockyard. When doing work on this code, familiarity with the
dockyard_host will be very beneficial.
