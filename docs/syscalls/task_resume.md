# mx_task_resume

## NAME

task_resume - resume the given task

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_task_resume(mx_handle_t task, uint32_t options);

```

## DESCRIPTION

**task_resume**() causes the requested task to resume execution, either from
an exception or from having been suspended.

### RESUMING FROM SUSPEND

If **MX_RESUME_EXCEPTION** is not set in *options*, the operation is a
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
the task is being resumed from an exception: **MX_RESUME_EXCEPTION**.

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
mx_status_t status = mx_task_resume(thread, MX_RESUME_EXCEPTION);
```

To pass the exception on to the next handler in the search order,
pass **MX_RESUME_TRY_NEXT** in addition to
**MX_RESUME_EXCEPTION**:

```
mx_status_t status = mx_task_resume(thread,
                                    MX_RESUME_EXCEPTION |
                                    MX_RESUME_TRY_NEXT);
```

Note that even though exceptions are sent to handlers in a specific
order, there is no way for the caller of **mx_task_resume**()
to verify it is that handler. Anyone with appropriate rights
can resume a thread from an exception. It is up to exception
handlers to not trip over each other, as well as all other
software calling **mx_task_resume**() with **MX_RESUME_EXCEPTION**.
(MG-562 documents this issue.)

## RETURN VALUE

**task_resume**() returns **MX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE** *handle* is not a thread handle.

**MX_ERR_BAD_STATE**  The task is not in a state where resuming is possible (e.g.
it is dead or **MX_RESUME_EXCEPTION** was passed but the thread is not in an
exception).

**MX_ERR_INVALID_ARGS** *options* is not a valid combination.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

[task_suspend](task_suspend.md),
