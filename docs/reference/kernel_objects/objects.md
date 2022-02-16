# Zircon Kernel objects

[TOC]

Zircon is an object-based kernel. User mode code almost exclusively interacts
with OS resources via object [handles]. A handle can be thought of as an active
session with a specific OS subsystem scoped to a particular resource.

Zircon actively manages the following resources:

+ processor time
+ memory and address spaces
+ device-io memory
+ interrupts
+ signaling and waiting
+ inter-process communication

## Kernel objects for applications

### IPC

+ [Channel](/docs/reference/kernel_objects/channel.md)
+ [Socket](/docs/reference/kernel_objects/socket.md)
+ [FIFO](/docs/reference/kernel_objects/fifo.md)

### Tasks

+ [Process](/docs/reference/kernel_objects/process.md)
+ [Thread](/docs/reference/kernel_objects/thread.md)
+ [Job](/docs/reference/kernel_objects/job.md)
+ [Task](/docs/reference/kernel_objects/task.md)

### Scheduling

+ [Profile](/docs/reference/kernel_objects/profile.md)

### Signaling

+ [Event](/docs/reference/kernel_objects/event.md)
+ [Event Pair](/docs/reference/kernel_objects/eventpair.md)
+ [Futex](/docs/reference/kernel_objects/futex.md)

### Memory and address space

+ [Virtual Memory Object](/docs/reference/kernel_objects/vm_object.md)
+ [Virtual Memory Address Region](/docs/reference/kernel_objects/vm_address_region.md)
+ [bus_transaction_initiator](/docs/reference/kernel_objects/bus_transaction_initiator.md)
+ [Pager](/docs/reference/kernel_objects/pager.md)

### Waiting

+ [Port](/docs/reference/kernel_objects/port.md)
+ [Timer](/docs/reference/kernel_objects/timer.md)

## Kernel objects for drivers

+ [Interrupts](/docs/reference/kernel_objects/interrupts.md)
+ [Message Signaled Interrupts](/docs/reference/kernel_objects/msi.md)
+ [Resource](/docs/reference/kernel_objects/resource.md)
+ [Debuglog](/docs/reference/kernel_objects/debuglog.md)

## Kernel object lifetime

Kernel objects are [reference-counted]. Most kernel objects are
created during a 'create' syscall and are held alive by the first handle,
given as the output of the create syscall. The caller gets the numeric id of
the handle and the handle itself is placed in the handle table of the process.

A handle is held alive as long it exists in the handle table. Handles are
removed from the handle table by:

+ Closing them via [`zx_handle_close`] which decrements the reference
count of the corresponding kernel object. Usually, when the last handle is
closed the kernel object reference count will reach 0 which causes the kernel
object to be destroyed.

+ When the process that owns the handle table is destroyed. The kernel
effectively iterates over the entire handle table closing each handle in turn.

The reference count increases when new handles (referring to the same object)
are created via [`zx_handle_duplicate`], but also when a direct pointer
reference (by some kernel code) is acquired; therefore a kernel object lifetime
might be longer than the lifetime of the code that created it.

There are three important cases in which kernel objects are kept alive
when there are no outstanding handles to them:

+ The object is referenced by a message which has not been consumed. This
can happen via the [channel APIs][channel-api]. While such message is in the
channel the kernel keeps the object alive.

+ The object is the parent of another object which is alive. This is the
case of [VMOs] attached to live [VMARs], of processes with live [threads] and
[jobs] with live processes or child jobs.

+ Threads are kept alive by the scheduler. A thread that is alive will continue
to live until it voluntarily exits by calling [`zx_thread_exit`] or the process
is terminated via [`zx_task_kill`].

The outcome of the last case is that a single thread can keep its process
and the entire lineage of jobs up to the root job alive.

## Kernel Object security

Kernel objects do not have an intrinsic notion of security and do not do
authorization checks; security rights are held by each handle. A single process
can have two different handles to the same object with different rights.

## See Also

[Handles][handles]

[handles]: /docs/concepts/kernel/handles.md
[reference-counted]: https://en.wikipedia.org/wiki/Reference_counting
[`zx_handle_close`]: /docs/reference/syscalls/handle_close.md
[`zx_handle_duplicate`]: /docs/reference/syscalls/handle_duplicate.md
[`zx_thread_exit`]:/docs/reference/syscalls/thread_exit.md
[`zx_task_kill`]: /docs/reference/syscalls/task_kill.md
[VMOs]: /docs/reference/kernel_objects/vm_object.md
[VMARs]: /docs/reference/kernel_objects/vm_address_region.md
[threads]: /docs/reference/kernel_objects/thread.md
[jobs]: /docs/reference/kernel_objects/job.md
[channel-api]: /docs/reference/kernel_objects/channel.md
