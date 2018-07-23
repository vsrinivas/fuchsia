# zx_task_resume_from_exception

## NAME

task_resume_from_exception - resume the given task after an exception has been
reported

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_task_resume_from_exception(zx_handle_t task, zx_handle_t port, uint32_t options);

```

## DESCRIPTION

**task_resume_from_exception**() causes the requested task to resume after an
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
See [task_bind_exception_port](task_bind_exception_port.md)
for a description of exception processing.

To resume a thread where it left off, pass 0 for the options:

```
zx_status_t status = zx_task_resume_from_exception(thread, 0);
```

To pass the exception on to the next handler in the search order,
pass **ZX_RESUME_TRY_NEXT** for the options.

```
zx_status_t status = zx_task_resume_from_exception(thread, ZX_RESUME_TRY_NEXT);
```

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**task_resume_from_exception**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not a thread handle.

**ZX_ERR_BAD_STATE**  The task is not in a state where resuming is possible,
for example, it is dead or there is not an exception to resume from.

**ZX_ERR_INVALID_ARGS** *options* is valid.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

[task_bind_exceptino_port](task_bind_exception_port.md),
