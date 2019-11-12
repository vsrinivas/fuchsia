# Zircon

Zircon is the core platform that powers the Fuchsia OS.  Zircon is
composed of a microkernel (source in [/zircon/kernel](/zircon/kernel)
as well as a small set of userspace services, drivers, and libraries
(source in [/zircon/system/](/zircon/system) necessary for the system
to boot, talk to hardware, load userspace processes and run them, etc.
Fuchsia builds a much larger OS on top of this foundation.

The canonical Zircon repository part of the Fuchsia project
at: [https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/zircon/](/zircon/)

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

+ [Getting Started](/docs/development/kernel/getting_started.md)
+ [Contributing Patches](/docs/contribute/contributing.md)
+ [GN in Zircon](/docs/development/build/zircon_gn.md)

+ [Concepts Overview](/docs/concepts/kernel/concepts.md)
+ [Kernel Objects](/docs/concepts/objects/objects.md)
+ [Kernel Invariants](kernel_invariants.md)
+ [Kernel Scheduling](kernel_scheduling.md)
+ [Fair Scheduler](fair_scheduler.md)
+ [Errors](errors.md)
+ [Time](/docs/concepts/objects/time.md)

+ [Process Objects](/docs/concepts/objects/process.md)
+ [Thread Objects](/docs/concepts/objects/thread.md)
+ [Thread local storage](/docs/development/threads/tls.md)
+ [Thread annotations](/docs/development/threads/thread_annotations.md)
+ [Handles](/docs/concepts/objects/handles.md)
+ [Lock validation](lockdep.md)
+ [System Calls](/docs/reference/syscalls/README.md)
+ [zxcrypt](/docs/concepts/filesystems/zxcrypt.md)

+ [Driver Development Kit](/docs/concepts/drivers/overview.md)
+ [Driver interfaces - audio overview](/docs/concepts/drivers/driver_interfaces/audio_overview.md)

+ [libc](/docs/development/languages/c-cpp/libc.md)
+ [C++ fit::promise<> guide](/docs/development/languages/c-cpp/fit_promise_guide.md)

+ [Testing](/docs/development/testing/testing.md)
+ [Kernel tracing](/docs/development/tracing/ktrace.md)
+ [Block device testing](/docs/development/testing/block_device_testing.md)
+ [nand Testing](/docs/development/testing/nand_testing.md)

+ [Compile-time object collections](/docs/development/languages/c-cpp/compile_time_object_collections.md)
+ [ACPI debugging](/docs/development/debugging/acpi.md)
+ [Fuzzing the FIDL host tools](/docs/development/testing/fuzzing/fuzzing_fidl.md)
+ [Entropy collection TODOs](/docs/concepts/system/jitterentropy/entropy_collection_todos.md)
+ [Memory usage analysis tools](/docs/development/memory/memory.md)
+ [Symbolizer](/docs/reference/kernel/symbolizer_markup.md)
+ [Relationship with LK](zx_and_lk.md)
+ [Micro-benchmarks](/docs/development/benchmarking/microbenchmarks.md)
+ [Avoiding a problem with the SYSRET instruction](sysret_problem.md)
