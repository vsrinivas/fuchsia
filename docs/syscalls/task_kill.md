# mx_task_kill

## NAME

task_kill - Kill the provided task.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_task_kill(mx_handle_t handle);

```

## DESCRIPTION

This kills the given process, thread or job.

If a process or thread uses this syscall to kill itself, this syscall does
not return.

## RETURN VALUE

## ERRORS

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_DESTROY**.

## SEE ALSO
