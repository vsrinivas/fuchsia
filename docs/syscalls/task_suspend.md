# zx_task_suspend

This function is deprecated. Please use
[zx_task_suspend_token](task_suspend_token.md). When all callers have been
updated, this variant will be deleted and **task_suspend_token** will be
renamed to **task_suspend**.

## NAME

task_suspend - suspend the given task

## SYNOPSIS

```
#include <zircon/syscalls.h>

void zx_task_suspend(zx_handle_t task);

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

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**task_suspend**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not a thread handle.

**ZX_ERR_BAD_STATE**  The task is not in a state where suspending is possible.

## LIMITATIONS

Currently only thread handles are supported.

## SEE ALSO

[task_resume](task_resume.md),
