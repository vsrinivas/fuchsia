# zx_task_kill

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

task_kill - Kill the provided task (job, process, or thread).

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_task_kill(zx_handle_t handle);
```

## DESCRIPTION

This asynchronously kills the given process, thread or job and its children
recursively, until the entire task tree rooted at *handle* is dead.

It is possible to wait for the task to be dead via the **ZX_TASK_TERMINATED**
signal. When the procedure completes, as observed by the signal, the task and
all its children are considered to be in the dead state and most operations
will no longer succeed.

If *handle* is a job and the syscall is successful, the job cannot longer be
used to create new processes.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must have **ZX_RIGHT_DESTROY**.

## RETURN VALUE

On success, `zx_task_kill()` returns **ZX_OK**. If a process or thread uses
this syscall to kill itself, this syscall does not return.

## NOTES

When using this syscall on a process, the return code for the process
is -1 as reported by [`zx_object_get_info()`] via the **ZX_INFO_PROCESS** topic.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a task handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have the **ZX_RIGHT_DESTROY**
right.

## SEE ALSO

 - [`zx_job_create()`]
 - [`zx_process_create()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_job_create()`]: job_create.md
[`zx_object_get_info()`]: object_get_info.md
[`zx_process_create()`]: process_create.md
