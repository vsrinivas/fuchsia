# Zircon

Zircon is the core platform that powers the Fuchsia OS.  Zircon is
composed of a microkernel (source in kernel/...) as well as a small
set of userspace services, drivers, and libraries (source in system/...)
necessary for the system to boot, talk to hardware, load userspace
processes and run them, etc.  Fuchsia builds a much larger OS on top
of this foundation.

The canonical Zircon Git repository is located
at: https://fuchsia.googlesource.com/zircon

A read-only mirror of the code is present
at: https://github.com/fuchsia-mirror/zircon

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

+ [Getting Started](docs/getting_started.md)
+ [Contributing Patches](docs/contributing.md)

+ [Concepts Overview](docs/concepts.md)
+ [Kernel Objects](docs/objects.md)
+ [Process Objects](docs/objects/process.md)
+ [Thread Objects](docs/objects/thread.md)
+ [Handles](docs/handles.md)
+ [System Calls](docs/syscalls.md)

+ [Driver Development Kit](docs/ddk/overview.md)

+ [Testing](docs/testing.md)
+ [Hacking notes](docs/hacking.md)
+ [Memory usage analysis tools](docs/memory.md)
+ [Relationship with LK](docs/zx_and_lk.md)
+ [Micro-benchmarks](docs/benchmarks/microbenchmarks.md)
