# Magenta Handles

[TOC]

## Basics
Handles are kernel constructs that allows user-mode programs to
reference a kernel object. A handle can be thought as a session
or connection to a particular kernel object.

It is often the case that multiple processes concurrently access
the same object via different handles. However, a single handle
can only be either bound to a single process or be bound to
kernel.

When it is bound to kernel we say it's 'in-transit'.

In user-mode a handle is simply a specific number returned by
some syscall. Only handles that are not in-transit are visible
to user-mode.

The integer that represents a handle is only meaningful for that
process. The same number in another process might not map to any
handle or it might map to a handle pointing to a completely
different kernel object.

For kernel-mode, a handle is a C++ object that contains three
logical fields:

+ A reference to a kernel object
+ The rights to the kernel object
+ The process it is bound to (or if it's bound to kernel)

The '[rights](rights.md)' specify what operations on the kernel object are
allowed. It is possible for a single process to have two different
handles to the same kernel object with different rights.

## Using Handles
There are many syscalls that create a new kernel object
and which return a handle to it. To name a few:
+ `mx_event_create`
+ `mx_process_create`
+ `mx_thread_create`

These calls create both the kernel object and the first
handle pointing to it. The handle is bound to the process that
issued the syscall and the rights are the default rights for
that type of kernel object.

There is only one syscall that can make a copy of a handle,
which points to the same kernel object and is bound to the same
process that issued the syscall:
+ `mx_handle_duplicate`

There is one syscall that creates an equivalent handle (possibly
with fewer rights), invalidating the original handle:
+ `mx_handle_replace`

There is one syscall that just destroys a handle:
+ `mx_handle_close`

There is only one syscall that takes a handle bound to calling
process and binds it into kernel (puts the handle in-transit):
+ `mx_channel_write`

There is only one syscall that takes an in-transit handle and
binds it to the calling process:
+ `mx_channel_read`

The pair of channel syscalls above are used to transfer a handle
from one process to another. The gist is that it is possible
to connect two processes with a channel. To transfer a handle
the source process calls `mx_channel_write` and then the
destination process calls `mx_channel_read` on the same channel.

Finally, there is a single syscall that gives a new process its
bootstrapping handle, that is, the handle that it can use to
request other handles:
+ `mx_process_start`

It is natural that the bootstrapping handle points to one end of a
channel.

## Garbage Collection
If a handle is valid, the kernel object it points to is guaranteed
to be valid. This is ensured because kernel objects are ref-counted
and each handle holds a reference to its kernel object.

The opposite does not hold. When a handle is destroyed it does not
mean its object is destroyed. There could be other handles pointing
to the object or the kernel itself could be holding a reference to
the kernel object.

When there is but one handle referencing a kernel object and the
handle is being closed, the kernel object is either destroyed then or
the kernel marks the object for garbage collection; the object will be
destroyed when the current set of operations on it are completed.

## Special Cases
+ When a handle is in-transit and the channel it was written to
is destroyed, the handle is closed.
+ Debugging sessions (and debuggers) might have special syscalls to
get access to handles.

## See Also
[Objects](objects.md),
[Rights](rights.md)
