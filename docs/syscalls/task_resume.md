# zx_task_resume

This function is deprecated. When you suspend a thread with
[task_suspend_token](task_suspend_token.md) closing the suspend token will
automatically resume the thread.

## NAME

task_resume - resume the given task

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_task_resume(zx_handle_t task, uint32_t options);

```

## DESCRIPTION

**task_resume**() causes the requested task to resume execution, either from
an exception or from having been suspended.

### RESUMING FROM SUSPEND

If **ZX_RESUME_EXCEPTION** is not set in *options*, the operation is a
resume-from-suspend.

It is an error for *options* to have any options selected in this mode.

**task_resume**() is invoked on a task that is not suspended but is living,
it will still return success.  If **task_resume**() is invoked multiple times
on the same suspended task, the calls are coalesced into a single resume.

The task will be resumed soon after **task_resume**() is invoked.  If
**task_suspend**() had been issued already, but the task had not suspended
yet, **task_resume**() cancels the pending suspend.  If a task has successfully
suspended already, **task_resume**() will resume it immediately.  A subsequent
**task_suspend**() will cause a new suspend to occur.

### RESUMING FROM EXCEPTION

Resuming from exceptions uses the same mechanism as resuming from
suspensions: **task_resume**(). An option is passed specifying that
the task is being resumed from an exception: **ZX_RESUME_EXCEPTION**.

Note that a thread can be both suspended and in an exception, each
requiring separate calls to **task_resume**() with appropriate options.

There are two ways to resume from an exception, depending on whether
one wants the thread to resume where it left off, which in the case
of an architectural exception generally means retrying the offending
instruction, or give the next handler in the search order a chance
to handle the exception.
See [task_bind_exception_port](task_bind_exception_port.md)
for a description of exception processing.

To resume a thread where it left off:

```
zx_status_t status = zx_task_resume(thread, ZX_RESUME_EXCEPTION);
```

To pass the exception on to the next handler in the search order,
pass **ZX_RESUME_TRY_NEXT** in addition to
**ZX_RESUME_EXCEPTION**:

```
zx_status_t status = zx_task_resume(thread,
                                    ZX_RESUME_EXCEPTION |
                                    ZX_RESUME_TRY_NEXT);
```

Note that even though exceptions are sent to handlers in a specific
order, there is no way for the caller of **zx_task_resume**()
to verify it is that handler. Anyone with appropriate rights
can resume a thread from an exception. It is up to exception
handlers to not trip over each other, as well as all other
software calling **zx_task_resume**() with **ZX_RESUME_EXCEPTION**.
(ZX-562 documents this issue.)

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**task_resume**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not a thread handle.

**ZX_ERR_BAD_STATE**  The task is not in a state where resuming is possible (e.g.
it is dead or **ZX_RESUME_EXCEPTION** was passed but the thread is not in an
exception).

**ZX_ERR_INVALID_ARGS** *options* is not a valid combination.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

[task_suspend](task_suspend.md),
