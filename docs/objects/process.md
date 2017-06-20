# Process

## NAME

process - Process abstraction

## SYNOPSIS

A magenta process is an instance of a program in the traditional
sense: a set of instructions which will be executed by one or more
threads, along with a collection of resources.

## DESCRIPTION

The process object is a container of the following resources:

+ [Handles](../handles.md)
+ [Virtual Memory Address Regions](vm_address_region.md)
+ [Threads](thread.md)

In general, it is associated with code which it is executing until it is
forcefully terminated or the program exits.

Processes are owned by [jobs](job.md) and allow an application that is
composed by more than one process to be treated as a single entity, from the
perspective of resource and permission limits, as well as lifetime control.

### Lifetime
A process is created via `sys_process_create()` which take no parameters.
The process starts destruction when main thread terminates or the last handle
is closed. [⚠ not implemented].

Next, the main binary is loaded into the process via `sys_process_load()` and
its execution begins with `sys_process_start()`.

## SYSCALLS

+ [process_create](../syscalls/process_create.md) - create a new process within a job
+ [process_read_memory](../syscalls/process_read_memory.md) - read from a process's address space
+ [process_start](../syscalls/process_start.md) - cause a new process to start executing
+ [process_write_memory](../syscalls/process_write_memory.md) - write to a process's address space
+ [process_exit](../syscalls/process_exit.md) - exit the current process

<br>

+ [job_create](../syscalls/job_create.md) - create a new job within a parent job

<br>

+ [vmar_map](../syscalls/vmar_map.md) - Map memory into an address space range
+ [vmar_protect](../syscalls/vmar_protect.md) - Change permissions on an address space range
+ [vmar_unmap](../syscalls/vmar_unmap.md) - Unmap memory from an address space range
