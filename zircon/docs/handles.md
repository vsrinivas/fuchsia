# Zircon Handles

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

The integer value for a handle is any 32-bit number except
the value corresponding to ZX_HANDLE_INVALID.

For kernel-mode, a handle is a C++ object that contains three
logical fields:

+ A reference to a kernel object
+ The rights to the kernel object
+ The process it is bound to (or if it's bound to kernel)

The '[rights](rights.md)' specify what operations on the kernel object
are allowed. It is possible for a single process to have two different
handles to the same kernel object with different rights.

## Using Handles
There are many syscalls that create a new kernel object
and which return a handle to it. To name a few:
+ [`zx_event_create`](syscalls/event_create.md)
+ [`zx_process_create`](syscalls/process_create.md)
+ [`zx_thread_create`](syscalls/thread_create.md)

These calls create both the kernel object and the first
handle pointing to it. The handle is bound to the process that
issued the syscall and the rights are the default rights for
that type of kernel object.

There is only one syscall that can make a copy of a handle,
which points to the same kernel object and is bound to the same
process that issued the syscall:
+ [`zx_handle_duplicate`](syscalls/handle_duplicate.md)

There is one syscall that creates an equivalent handle (possibly
with fewer rights), invalidating the original handle:
+ [`zx_handle_replace`](syscalls/handle_replace.md)

There is one syscall that just destroys a handle:
+ [`zx_handle_close`](syscalls/handle_close.md)

There are two syscalls that takes a handle bound to calling
process and binds it into kernel (puts the handle in-transit):
+ [`zx_channel_write`](syscalls/channel_write.md)
+ [`zx_socket_share`](syscalls/socket_share.md)

There are three syscalls that takes an in-transit handle and
binds it to the calling process:
+ [`zx_channel_read`](syscalls/channel_read.md)
+ [`zx_channel_call`](syscalls/channel_call.md)
+ [`zx_socket_accept`](syscalls/socket_accept.md)

The channel and socket syscalls above are used to transfer a handle from
one process to another. For example it is possible to connect
two processes with a channel. To transfer a handle the source process
calls `zx_channel_write` or `zx_channel_call` and then the destination
process calls `zx_channel_read` on the same channel.

Finally, there is a single syscall that gives a new process its
bootstrapping handle, that is, the handle that it can use to
request other handles:
+ [`zx_process_start`](syscalls/process_start.md)

The bootstrapping handle can be of any transferable kernel object but
the most reasonable case is that it points to one end of a channel
so this initial channel can be used to send further handles into the
new process.

## Garbage Collection
If a handle is valid, the kernel object it points to is guaranteed
to be valid. This is ensured because kernel objects are ref-counted
and each handle holds a reference to its kernel object.

The opposite does not hold. When a handle is destroyed it does not
mean its object is destroyed. There could be other handles pointing
to the object or the kernel itself could be holding a reference to
the kernel object. An example of this is a handle to a thread; the
fact that the last handle to a thread is closed it does not mean that
the thread has been terminated.

When the last reference to a kernel object is released, the kernel
object is either destroyed or the kernel marks the object for
garbage collection; the object will be destroyed at a later time
when the current set of pending operations on it are completed.

## Special Cases
+ When a handle is in-transit and the channel or socket it was written
to is destroyed, the handle is closed.
+ Debugging sessions (and debuggers) might have special syscalls to
get access to handles.

## Invalid Handles and handle reuse

It is an error to pass to any syscall except for `zx_object_get_info`
the following values:

+ A handle value that corresponds to a closed handle
+ The **ZX_HANDLE_INVALID** value, except for `zx_handle_close` syscall

The kernel is free to re-use the integer values of closed handles for
newly created objects. Therefore, it is important to make sure that proper
handle hygiene is observed:

+ Don't have one thread close a given handle and another thread use the
  same handle in a racy way. Even if the second thread is also closing it.
+ Don't ignore **ZX_ERR_BAD_HANDLE** return codes. They usually mean the
  code has a logic error.

Detecting invalid handle usage can be automated by using the
**ZX_POL_BAD_HANDLE** Job policy with **ZX_POL_ACTION_EXCEPTION** to
generate an exception when a process under such job object attempts any of
the of the mentioned invalid cases.

## See Also
[Objects](objects.md),
[Rights](rights.md)
