# zx_task_suspend

This function replaces [task_suspend](task_suspend.md). When all callers are
updated, **task_suspend** will be deleted and this function will be renamed
**task_suspend**.

## NAME

task_suspend_token - suspend the given task. Currently only thread handles may
be suspended.

## SYNOPSIS

```
#include <zircon/syscalls.h>

void zx_task_suspend_token(zx_handle_t task, zx_handle_t* suspend_token);

```

## DESCRIPTION

**task_suspend_token**() causes the requested task to suspend execution. Task
suspension is not synchronous and the task might not be suspended before the
call returns. The task will be suspended soon after **task_suspend_token**() is
invoked, unless it is currently blocked in the kernel, in which case it will
suspend after being unblocked.

Invoking **task_kill**() on a task that is suspended will successfully kill
the task.

## RESUMING

The allow the task to resume, close the suspend token handle. The task will
remain suspended as long as there are any open suspend tokens. Like suspending,
resuming is asynchronous so the thread may not be in a running state when the
[handle_close](handle_close.md) call returns, even if no other suspend tokens
are open.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**task_suspend**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not a thread handle.

**ZX_ERR_INVALID_ARGS**  *suspend_token*  was an invalid pointer.

**ZX_ERR_BAD_STATE**  The task is not in a state where suspending is possible.

## LIMITATIONS

Currently only thread handles are supported.
