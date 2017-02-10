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

TODO: Document this

## RETURN VALUE

**task_resume**() returns **NO_ERROR** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE** *handle* is not a valid handle.

**ERR_WRONG_TYPE** *handle* is not a thread handle.

**ERR_BAD_STATE**  The task is not in a state where resuming is possible (e.g.
it is dead)

**ERR_INVALID_ARGS** *options* is not a valid combination.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

[task_suspend](task_suspend.md),
