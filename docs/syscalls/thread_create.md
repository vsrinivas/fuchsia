# mx_thread_create

## NAME

thread_create - create a thread

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_handle_t mx_thread_create(mx_handle_t process, const char* name,
                             uint32_t name_len, uint32_t flags);

```

## DESCRIPTION

**thread_create**() creates a thread within the specified process.

Upon success a handle for the new thread is returned.  The thread
will not start executing until *thread_start()* is called.

When the last handle to a thread is closed, the thread is destroyed.

Thread handles may be waited on and will assert the signal
*MX_SIGNAL_SIGNALED* when the thread stops executing (due to
*thread_exit**() being called.

## RETURN VALUE

**thread_create**() returns a handle to the new thread on succes.
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
[thread_exit](thread_exit.md),
[thread_start](thread_start.md).
