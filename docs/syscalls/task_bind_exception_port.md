# mx_task_bind_exception_port

## NAME

task_bind_exception_port - Bind to, or unbind from, the exception port
corresponding to a given job, process, thread, or the system exception port.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_task_bind_exception_port(mx_handle_t object, mx_handle_t eport,
                                          uint64_t key, uint32_t options);
```

## DESCRIPTION

**task_bind_exception_port**() is used to bind (or unbind) a port to
the exception port of a job, process, thread, or the system exception port.

To bind to the system exception port pass **MX_HANDLE_INVALID** for *object*.

*eport* is an IO port created by [mx_port_create](port_create.md). The same
IO port can be bound to multiple objects.

*key* is passed back in exception reports, and is part of the port
message protocol.

When a port is bound to the exception port of an object it participates
in exception processing. See below for how exceptions are processed.

### Unbinding

To unbind from an exception port pass **MX_HANDLE_INVALID** for *eport*.
This will remove the exception port from *object* and *eport* will no
longer participate in exception processing for *object*.

To unbind from the system exception port pass **MX_HANDLE_INVALID** for
*object*.

The exception port will unbind automatically if all handles to *eport*
are closed while it is still bound.

A thread may be currently waiting for a response from the program that
bound *eport* when it is unbound. There are two choices for what happens
to the thread:

- Have exception processing continue as if *eport* had never been bound.
This is the default behavior.

- Have the thread continue to wait for a response from the same kind
of exception port as *eport*. This is done by passing
*MX_EXCEPTION_PORT_UNBIND_QUIETLY* in *options* when unbinding *eport*.
This option is useful, for example, when a debugger wants to detach from the
thread's process, but leave the thread in stasis waiting for an exception
response.

### Exception processing

When a thread gets an exception it is paused while the kernel processes
the exception. The kernel looks for bound exception ports in a specific order
and if it finds one an "exception report" is sent to the bound port.
Then when the "exception handler" that bound the port is finished processing
the exception it "resumes" the thread with the **task_resume**() system call.

Resuming the thread can be done in either of two ways:

- Resume execution of the thread as if the exception has been resolved.
If the thread gets another exception then exception processing begins
again anew.

- Resume exception processing, marking the exception as "unhandled", giving
the next exception port in the search order a chance to process the exception.

Exception reports are messages sent through the port with a specific format
defined by the port message protocol. The packet contents are defined by
the *mx_exception_packet_t* type defined in magenta/syscalls/port.h.

### Exception search order

Exception ports are searched in the following order:

- Debugger - The debugger exception port is associated with processes, and
is for things like gdb. To bind to the debugger exception port
pass *MX_EXCEPTION_PORT_DEBUGGER* in *options* when binding an
exception port to the process.
There is only one debugger exception port per process.

- Thread - This is for exception ports bound directly to the thread.
There is only one thread exception port per thread.

- Process - This is for exception ports bound directly to the process.
There is only one process exception port per process.

- Job - This is for exception ports bound to the process's job. Note that jobs
have a hierarchy. First the process's job is searched. If it has a bound
exception port then the exception is delivered to that port. If it does not
have a bound exception port, or if the handler returns **MX_RESUME_TRY_NEXT**,
then that job's parent job is searched, and so on right up to the root job.

- System - This is the last port searched and gives the system a chance to
process the exception before the kernel kills the process.

If no exception port handles the exception then the kernel finishes
exception processing by killing the process.

### Types of exceptions

At a high level there are two types of exceptions: architectural and synthetic.
Architectural exceptions are things like a segment fault (e.g., dereferencing
the NULL pointer) or executing an undefined instruction. Synthetic exceptions
are things like thread start and stop notifications.

Exception types are enumerated in the *mx_excp_type_t* enum defined
in magenta/syscalls/exception.h.

## RETURN VALUE

**task_bind_exception_port**() returns **MX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE** *object* is not a valid handle,
or *eport* is not a valid handle. Note that when binding/unbinding
to the system exception port *object* is **MX_HANDLE_INVALID**.
Also note that when unbinding from an exception port *eport* is
**MX_HANDLE_INVALID**.

**MX_ERR_WRONG_TYPE**  *object* is not that of a job, process, or thread,
and is not **MX_HANDLE_INVALID**,
or *eport* is not that of a port and is not **MX_HANDLE_INVALID**.

**MX_ERR_INVALID_ARGS** A bad value has been passed in *options*.

**MX_ERR_NO_MEMORY**  (temporary) out of memory failure.

## SEE ALSO

[port_create](port_create.md).
[port_wait](port_wait.md).
[task_resume](task_resume.md).
