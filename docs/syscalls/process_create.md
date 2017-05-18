# mx_process_create

## NAME

process_create - create a new process

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_process_create(mx_handle_t job,
                              const char* name, uint32_t name_len,
                              uint32_t options,
                              mx_handle_t* proc_handle, mx_handle_t* vmar_handle);

```

## DESCRIPTION

**process_create**() creates a new process.

Upon success, handles for the new process and the root of its address space
are returned.  The thread will not start executing until *process_start()* is
called.

*name* is silently truncated to a maximum of *MX_MAX_NAME_LEN-1* characters.

When the last handle to a process is closed, the process is destroyed.

Process handles may be waited on and will assert the signal
*MX_PROCESS_TERMINATED* when the process exits.

*job* is the controlling [job object](../objects/job.md) for the new
process, which will become a child of that job.

## RETURN VALUE

On success, **process_create**() returns **NO_ERROR**, a handle to the new process
(via *proc_handle*), and a handle to the root of its address space (via
*vmar_handle*).  In the event of failure, a negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *job* is not a valid handle.

**ERR_WRONG_TYPE**  *job* is not a job handle.

**ERR_ACCESS_DENIED**  *job* does not have the **MX_WRITE_RIGHT** right
(only when not **MX_HANDLE_INVALID**).

**ERR_INVALID_ARGS**  *name*, *proc_handle*, or *vmar_handle*  was an invalid pointer,
or *options* was non-zero.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BAD_STATE**  (Temporary) Failure due to the job object being in the
middle of a *mx_task_kill()* operation.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[process_start](process_start.md),
[task_kill](task_kill.md),
[thread_create](thread_create.md),
[thread_exit](thread_exit.md),
[thread_start](thread_start.md),
[job_create](job_create.md).
