# Process Object

## NAME

process - Process abstraction

## SYNOPSIS

A magenta process is an instance of a program in the traditional sense: a set
of instructions which will be executed by one or more threads.

## DESCRIPTION

The process object is a container of the following resources:

+ [Handles](../handles.md)
+ [Virtual Memory Address Regions](vm_address_region.md)
+ [Threads](thread.md)

In general, it is associated with code which it is executing until it is
forcefully terminated or the program exits.

Processes are owned by [jobs](job.md) and allow an application that is
composed by more than one process to be threated as a single entity, from the
perspective of resource and permission limits, as well as lifetime control.

### Lifetime
A process is created via `sys_process_create()` which take no parameters.
The process starts destruction when main thread terminates or the last handle
is closed. [⚠ not implemented].

Next, the main binary is loaded into the process via `sys_process_load()` and
its execution begins with `sys_process_start()`.

## SEE ALSO

[process_create](../syscalls/process_create.md),
[process_start](../syscalls/process_start.md),
[process_map_vm](../syscalls/process_map_vm.md),
[process_unmap_vm](../syscalls/process_unmap_vm.md),
[process_protect_vm](../syscalls/process_protect_vm.md),
[job_create](../syscalls/job_create.md).
