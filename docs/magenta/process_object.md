# Process Object
The process object, just like on other OSes is a container of the following
resources:

+ Handle table
+ Memory regions
+ [Threads](thread_object.md)

In general it operates more or less like a Linux or Windows process in
terms of being it associated with code (one or more ELF binaries) which it
is executing until it is forcefully terminated or the program exits.

## Lifetime
A process is created via `sys_process_create()` which take no parameters.
The process starts destruction when main thread terminates or the last handle
is closed. [⚠ not implemented].

Next, the main binary is loaded into the process via `sys_process_load()` and
its execution begins with `sys_process_start()`.

A thread in another process can wait for a process to exit with
`sys_process_join()`.
