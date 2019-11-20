# zx_task_resume_from_exception

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Resume the given task after an exception has been reported.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_task_resume_from_exception(zx_handle_t handle,
                                          zx_handle_t port,
                                          uint32_t options);
```

## DESCRIPTION

**Note: exception ports are deprecated and will be removed soon. See**
**[exceptions](/docs/concepts/kernel/exceptions.md) for information on the replacement API.**

`zx_task_resume_from_exception()` causes the requested task to resume after an
exception has been reported to the debug exception port. The port parameter
should identify the [exception port](task_bind_exception_port.md) to which the
exception being resumed from was delivered.

Note that if a thread has any open [suspend tokens](task_suspend_token.md), it
will remain suspended even when resumed from an exception.

There are two ways to resume from an exception, depending on whether
one wants the thread to resume where it left off, which in the case
of an architectural exception generally means retrying the offending
instruction, or give the next handler in the search order a chance
to handle the exception.
See [`zx_task_bind_exception_port()`] for a description of exception processing.

To resume a thread where it left off, pass 0 for the options:

```
zx_status_t status = zx_task_resume_from_exception(thread, port, 0);
```

To pass the exception on to the next handler in the search order,
pass **ZX_RESUME_TRY_NEXT** for the options.

```
zx_status_t status = zx_task_resume_from_exception(thread, port, ZX_RESUME_TRY_NEXT);
```

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_THREAD**.

*port* must be of type **ZX_OBJ_TYPE_PORT**.

## RETURN VALUE

`zx_task_resume_from_exception()` returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *port* is not the port from which the exception
report was sent.

**ZX_ERR_BAD_HANDLE** Either *handle* or *port* is not a valid handle.

**ZX_ERR_WRONG_TYPE** Either *handle* is not a thread handle,
or *port* is not a port handle.

**ZX_ERR_BAD_STATE**  The task is not in a state where resuming is possible,
for example, it is dead or there is not an exception to resume from.

**ZX_ERR_INVALID_ARGS** *options* is not valid.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

 - [`zx_task_bind_exception_port()`]

<!-- References updated by update-docs-from-fidl, do not edit. -->

[`zx_task_bind_exception_port()`]: task_bind_exception_port.md
