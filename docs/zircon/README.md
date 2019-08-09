# Zircon

Zircon is the core platform that powers the Fuchsia OS.  Zircon is
composed of a microkernel (source in [/zircon/kernel](/zircon/kernel)
as well as a small set of userspace services, drivers, and libraries
(source in [/zircon/system/](/zircon/system) necessary for the system
to boot, talk to hardware, load userspace processes and run them, etc.
Fuchsia builds a much larger OS on top of this foundation.

The canonical Zircon repository part of the Fuchsia project
at: [https://fuchsia.googlesource.com/fuchsia/zircon](https://fuchsia.googlesource.com/fuchsia/zircon)

The Zircon Kernel provides syscalls to manage processes, threads,
virtual memory, inter-process communication, waiting on object state
changes, and locking (via futexes).

Currently there are some temporary syscalls that have been used for early
bringup work, which will be going away in the future as the long term
syscall API/ABI surface is finalized.  The expectation is that there will
be about 100 syscalls.

Zircon syscalls are generally non-blocking.  The wait_one, wait_many
port_wait and thread sleep being the notable exceptions.

This page is a non-comprehensive index of the zircon documentation.

+ [Getting Started](getting_started.md)
+ [Contributing Patches](contributing.md)
+ [`abigen` grammar](abigen/grammar.md)
+ [Editors](editors.md)
+ [GN in Zircon](gn/zircon_gn.md)

+ [Concepts Overview](concepts.md)
+ [Kernel Objects](objects.md)
+ [Kernel Invariants](kernel_invariants.md)
+ [Kernel Scheduling](kernel_scheduling.md)
+ [Fair Scheduler](fair_scheduler.md)
+ [Errors](errors.md)
+ [Time](time.md)

+ [Process Objects](objects/process.md)
+ [Thread Objects](objects/thread.md)
+ [Thread local storage](tls.md)
+ [Thread annotations](thread_annotations.md)
+ [Handles](handles.md)
+ [Lock validation](lockdep.md)
+ [System Calls](syscalls.md)
+ [zxcrypt](zxcrypt.md)

+ [Driver Development Kit](ddk/overview.md)
+ [Driver interfaces - audio overview](driver_interfaces/audio_overview.md)

+ [libc](libc.md)
+ [C++ fit::promise<> guide](fit_promise_guide.md)

+ [Testing](testing.md)
+ [Kernel tracing](tracing/ktrace.md)
+ [Block device testing](block_device_testing.md)
+ [nand Testing](nand_testing.md)

+ [Compile-time object collections](compile_time_object_collections.md)
+ [ACPI debugging](debugging/acpi.md)
+ [Fuzzing the FIDL host tools](fuzzing_fidl.md)
+ [Hacking notes](hacking.md)
+ [Entropy collection TODOs](entropy_collection_todos.md)
+ [Memory usage analysis tools](memory.md)
+ [Symbolizer](symbolizer_markup.md)
+ [Static analysis](static_analysis.md)
+ [Relationship with LK](zx_and_lk.md)
+ [Micro-benchmarks](benchmarks/microbenchmarks.md)
+ [Avoiding a problem with the SYSRET instruction](sysret_problem.md)
