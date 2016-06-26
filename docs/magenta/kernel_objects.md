# Magenta Kernel objects

[TOC]

Magenta is a object-based kernel. User mode code almost exclusively interacts
with OS resources via object handles which map kernel objects to processes.

## Kernel objects in progress

+ [Process](process_object.md)
+ [Thread](thread_object.md)
+ Event
+ Message pipe
+ Interrupt request
+ Futex
+ VMObject

## Kernel objects planned

+ Data pipe
+ Job
+ IOPort

## Kernel Object and LK
Some kernel objects wrap one or more LK-level constructs. For example the
Thread object wraps one `thread_t`. However the Message Pipe does not wrap
any LK-level objects.

## Kernel object lifetime
Kernel objects are ref-counted. Most kernel objects are born during a syscall
and are held alive at refcount = 1 by the handle which binds the handle value
given as the output of the syscall. The handle object is held alive as long it
is attached to a handle table. Handles are detached from the handle table
closing them (for example via `sys_close()`) which decrements the refcount of
the kernel object. Usually, when the last handle is closed the kernel object
refcount will reach 0 which causes the destructor to be run.

The refcount increases both when new handles (referring to the object) are
created and when a direct pointer reference (by some kernel code) is acquired;
therefore a kernel object lifetime might be longer than the lifetime of the
process that created it.

## Dispatchers
A kernel object is implemented as a C++ class that derives from `Dispatcher`
and that overrides the methods it implements. Thus, for example, the code
of the Thread object is found in `ThreadDispatcher`. There is plenty of
code that only cares about kernel objects in the generic sense, in that case
the name you'll see is `utils::RefPtr<Dispatcher>`.

## Kernel Object security
In principle, kernel objects do not have an intrinsic notion of security and
do not do authorization checks; security rights are held by each handle. A
single process can have two different handles to the same object with
different rights.

