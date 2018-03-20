# zx_task_bind_exception_port

## NAME

task_bind_exception_port - Bind to, or unbind from, the exception port
corresponding to a given job, process, thread, or the system exception port.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_task_bind_exception_port(zx_handle_t object, zx_handle_t eport,
                                          uint64_t key, uint32_t options);
```

## DESCRIPTION

**task_bind_exception_port**() is used to bind (or unbind) a port to
the exception port of a job, process, or thread.

*eport* is an IO port created by [zx_port_create](port_create.md). The same
IO port can be bound to multiple objects.

*key* is passed back in exception reports, and is part of the port
message protocol.

When a port is bound to the exception port of an object it participates
in exception processing. See below for how exceptions are processed.

### Unbinding

To unbind from an exception port pass **ZX_HANDLE_INVALID** for *eport*.
This will remove the exception port from *object* and *eport* will no
longer participate in exception processing for *object*.

The exception port will unbind automatically if all handles to *eport*
are closed while it is still bound.

A thread may be currently waiting for a response from the program that
bound *eport* when it is unbound. There are two choices for what happens
to the thread:

- Have exception processing continue as if *eport* had never been bound.
This is the default behavior.

- Have the thread continue to wait for a response from the same kind
of exception port as *eport*. This is done by passing
*ZX_EXCEPTION_PORT_UNBIND_QUIETLY* in *options* when unbinding *eport*.
This option is useful, for example, when a debugger wants to detach from the
thread's process, but leave the thread in stasis waiting for an exception
response.

## RETURN VALUE

**task_bind_exception_port**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *object* is not a valid handle,
or *eport* is not a valid handle. Note that when unbinding from an exception
port *eport* is **ZX_HANDLE_INVALID**.

**ZX_ERR_WRONG_TYPE**  *object* is not that of a job, process, or thread,
and is not **ZX_HANDLE_INVALID**,
or *eport* is not that of a port and is not **ZX_HANDLE_INVALID**.

**ZX_ERR_INVALID_ARGS** A bad value has been passed in *options*.

**ZX_ERR_NO_MEMORY**  (temporary) out of memory failure.

## SEE ALSO

[exceptions](../exceptions.md).
[port_create](port_create.md).
[port_wait](port_wait.md).
[task_resume](task_resume.md).
