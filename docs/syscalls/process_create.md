# mx_process_create

## NAME

process_create - create a new process

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_handle_t mx_process_create(const char* name, uint32_t name_len, uint32_t flags);

```

## DESCRIPTION

**process_create**() creates a new process.

Upon success a handle for the new process is returned.  The thread
will not start executing until *process_start()* is called.

When the last handle to a process is closed, the process is destroyed.

Process handles may be waited on and will assert the signal
*MX_SIGNAL_SIGNALED* when the process exits.

## RETURN VALUE

**process_create**() returns a handle to the new process on succes.
In the event of failure, a negative error value is returned.

## ERRORS

**ERR_INVALID_ARGS**  *name* was an invalid pointer, or *name_len*
was greater than *MX_MAX_NAME_LEN*, or *flags* was non-zero.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[process_start](process_start.md),
[thread_create](thread_create.md),
[thread_exit](thread_exit.md),
[thread_start](thread_start.md).
