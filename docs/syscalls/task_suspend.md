# mx_task_suspend

## NAME

task_suspend - suspend the given task

## SYNOPSIS

```
#include <magenta/syscalls.h>

void mx_task_suspend(mx_handle_t task);

```

## DESCRIPTION

**task_suspend**() causes the requested task to suspend execution.  If
**task_suspend**() is invoked multiple times on the same task before
**task_resume**() is invoked, the calls are coalesced into a single suspend.

The task will be suspended soon after **task_suspend**() is invoked, unless
it is currently blocked in the kernel, in which case it will suspend after being
unblocked.

Invoking **task_kill**() on a task that is suspended will successfully kill
the task.

## RETURN VALUE

**task_suspend**() returns **MX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE** *handle* is not a thread handle.

**MX_ERR_BAD_STATE**  The task is not in a state where suspending is possible.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

[task_resume](task_resume.md),
