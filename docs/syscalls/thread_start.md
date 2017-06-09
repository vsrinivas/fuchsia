# mx_thread_start

## NAME

thread_start - start execution on a thread

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_thread_start(mx_handle_t thread, uintptr_t entry, uintptr_t stack,
                            uintptr_t arg1, uintptr_t arg2);
```

## DESCRIPTION

**thread_start**() causes a thread to begin execution at the program
counter specified by *entry* and with the stack pointer set to *stack*.
The arguments *arg1* and *arg2* are arranged to be in the architecture
specific registers used for the first two arguments of a function call
before the thread is started.  All other registers are zero upon start.

When the last handle to a thread is closed, the thread is destroyed.

Thread handles may be waited on and will assert the signal
*MX_THREAD_TERMINATED* when the thread stops executing (due to
*thread_exit**() being called.

## RETURN VALUE

**thread_start**() returns MX_OK on success.
In the event of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *thread* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *thread* is not a thread handle.

**MX_ERR_ACCESS_DENIED**  The handle *thread* lacks *MX_RIGHT_WRITE*.

**MX_ERR_BAD_STATE**  *thread* is not ready to run or the process *thread*
is part of is no longer alive.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[thread_create](thread_create.md),
[thread_exit](thread_exit.md).
