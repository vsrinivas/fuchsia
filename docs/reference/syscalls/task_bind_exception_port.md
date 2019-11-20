# zx_task_bind_exception_port

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Bind to, or unbind from, the exception port corresponding to a given job, process, or thread.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_task_bind_exception_port(zx_handle_t handle,
                                        zx_handle_t port,
                                        uint64_t key,
                                        uint32_t options);
```

## DESCRIPTION

**Note: exception ports are deprecated and will be removed soon. New code**
**should use [`zx_task_create_exception_channel()`] instead.**

`zx_task_bind_exception_port()` is used to bind (or unbind) a port to
the exception port of a job, process, or thread.

*port* is an IO port created by [`zx_port_create()`]. The same
IO port can be bound to multiple objects.

*key* is passed back in exception reports, and is part of the port
message protocol.

When a port is bound to the exception port of an object it participates
in exception processing. See below for how exceptions are processed.

### Unbinding

To unbind from an exception port pass **ZX_HANDLE_INVALID** for *port*.
This will remove the exception port from *handle* and *port* will no
longer participate in exception processing for *handle*.

The exception port will unbind automatically if all handles to *port*
are closed while it is still bound.

A thread may be currently waiting for a response from the program that
bound *port* when it is unbound. Exception processing will continue as if
*port* had never been bound.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

*port* must be of type **ZX_OBJ_TYPE_PORT**.

## RETURN VALUE

`zx_task_bind_exception_port()` returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_ALREADY_BOUND** *handle* already has its exception port bound.

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle,
or *port* is not a valid handle. Note that when unbinding from an exception
port *port* is **ZX_HANDLE_INVALID**.

**ZX_ERR_BAD_STATE** Unbinding a port that is not currently bound.

**ZX_ERR_WRONG_TYPE**  *handle* is not that of a job, process, or thread,
and is not **ZX_HANDLE_INVALID**,
or *port* is not that of a port and is not **ZX_HANDLE_INVALID**.

**ZX_ERR_INVALID_ARGS** A bad value has been passed in *options*.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

 - [exceptions](/docs/concepts/kernel/exceptions.md)
 - [`zx_port_create()`]
 - [`zx_port_wait()`]
 - [`zx_task_resume_from_exception()`]

<!-- References updated by update-docs-from-fidl, do not edit. -->

[`zx_port_create()`]: port_create.md
[`zx_port_wait()`]: port_wait.md
[`zx_task_create_exception_channel()`]: task_create_exception_channel.md
[`zx_task_resume_from_exception()`]: task_resume_from_exception.md
