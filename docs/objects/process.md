# Process Object

## NAME

process - Process abstraction

## SYNOPSIS

TODO

## DESCRIPTION

The process object is a container of the following resources:

+ Handle table
+ Memory regions
+ [Threads](thread.md)

In general, it is associated with code which it is executing until it is
forcefully terminated or the program exits.

### Lifetime
A process is created via `sys_process_create()` which take no parameters.
The process starts destruction when main thread terminates or the last handle
is closed. [⚠ not implemented].

Next, the main binary is loaded into the process via `sys_process_load()` and
its execution begins with `sys_process_start()`.

## SEE ALSO

TODO (add links to process syscall docs)
