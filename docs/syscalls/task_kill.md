# zx_task_kill

## NAME

task_kill - Kill the provided task (job, process, or thread).

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_task_kill(zx_handle_t handle);

```

## DESCRIPTION

This asynchronously kills the given process, thread or job. It is then
possible to wait for the task to be dead via the
**ZX_TASK_TERMINATED** signal.

If a process or thread uses this syscall to kill itself, this syscall does
not return.

## RETURN VALUE

## ERRORS

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_DESTROY**.

## SEE ALSO
