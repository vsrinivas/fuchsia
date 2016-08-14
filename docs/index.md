# Magenta

Magenta is the core platform that powers the Fuchsia OS.  Magenta is
composed of a microkernel as well as a small set of userspace services,
drivers, and libraries necessary for the system to boot, talk to hardware,
load userspace processes and run them, etc.  Fuchsia builds a much larger
OS on top of this foundation.

The Magenta Kernel is a medium-sized microkernel.  It provides services
(via syscalls) to manage processes, threads, virtual memory, inter-process
communication, waiting on object state changes, and locking (via futexes).

Currently there are some temporary syscalls that have been used for early
bringup work, which will be going away in the future as the long term
syscall API/ABI surface is finalized.  The expectation is that there will
be 10s, not 100s of syscalls.

Magenta syscalls are generally non-blocking.  The wait (one, many, set)
family of syscalls, ioport reads, and thread sleep being the notable
exceptions.

This page is a non-comprehensive index of the magenta documentation.

+ [Getting Started](getting_started.md)
+ [Relationship with LK](mg_and_lk.md)
+ [Kernel Objects](kernel_objects.md)
    + [Process Objects](objects/process.md)
    + [Thread Objects](objects/thread.md)
+ [Handles](handles.md)
+ [Futexes](futex.md)
+ [System Calls](syscalls.md)
