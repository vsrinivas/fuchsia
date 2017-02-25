# mx_futex_wake_handle_close_thread_exit

## NAME

futex_wake_handle_close_thread_exit - wake futex, close handle, exit

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_futex_wake_handle_close_thread_exit(
    const mx_futex_t* value_ptr, uint32_t wake_count,
    mx_handle_t close_handle);
```

## DESCRIPTION

**futex_wake_handle_close_thread_exit**() does a sequence of three operations:
1. `mx_futex_wake(value_ptr, wake_count);`
2. `mx_handle_close(close_handle);`
3. `mx_thread_exit();`

The expectation is that as soon as the *futex_wake* operation completes,
other threads may unmap or reuse the memory containing the calling
thread's own stack.  This is valid for this call, though it would be
invalid for plain *mx_futex_wake*() or any other call.

If the *futex_wake* operation is successful, then this call never returns.
If `close_handle` is an invalid handle so that the *handle_close* operation
fails, then the thread takes a trap (as if by `__builtin_trap();`).

## RETURN VALUE

**futex_wake_handle_close_thread_exit**() does not return on success.

## ERRORS

Same as [*mx_futex_wake*()](futex_wake.md).

## NOTES

The intended use for this is for a dying thread to alert another thread
waiting for its completion, close its own thread handle, and exit.
The thread handle cannot be closed beforehand because closing the last
handle to a thread kills that thread.  The *futex_wake* can't be done
first because the woken thread might reuse or deallocate this thread's
stack.

This call is used for joinable threads, while
[*vmar_unmap_handle_close_thread_exit*()](vmar_unmap_handle_close_thread_exit.md)
is used for detached threads.

## SEE ALSO

[futex_wake](futex_wake.md),
[handle_close](handle_close.md),
[thread_exit](thread_exit.md),
[vmar_unmap_handle_close_thread_exit](vmar_unmap_handle_close_thread_exit.md).
