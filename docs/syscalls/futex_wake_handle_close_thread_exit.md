# mx_futex_wake_handle_close_thread_exit

## NAME

futex_wake_handle_close_thread_exit - write to futex, wake futex, close handle, exit

## SYNOPSIS

```
#include <magenta/syscalls.h>

_Noreturn void mx_futex_wake_handle_close_thread_exit(
    mx_futex_t* value_ptr, uint32_t wake_count,
    int new_value, mx_handle_t close_handle);
```

## DESCRIPTION

**futex_wake_handle_close_thread_exit**() does a sequence of four operations:
1. `atomic_store_explicit(value_ptr, new_value, memory_order_release);`
2. `mx_futex_wake(value_ptr, wake_count);`
3. `mx_handle_close(close_handle);`
4. `mx_thread_exit();`

The expectation is that as soon as the first operation completes,
other threads may unmap or reuse the memory containing the calling
thread's own stack.  This is valid for this call, though it would be
invalid for plain *mx_futex_wake*() or any other call.

If any of the operations fail, then the thread takes a trap (as if by `__builtin_trap();`).

## RETURN VALUE

**futex_wake_handle_close_thread_exit**() does not return.

## ERRORS

None.

## NOTES

The intended use for this is for a dying thread to alert another thread
waiting for its completion, close its own thread handle, and exit.
The thread handle cannot be closed beforehand because closing the last
handle to a thread kills that thread.  The write to `value_ptr` can't be
done before this call because any time after the write, a joining thread might
reuse or deallocate this thread's stack, which may cause issues with calling
conventions into this function.

This call is used for joinable threads, while
[*vmar_unmap_handle_close_thread_exit*()](vmar_unmap_handle_close_thread_exit.md)
is used for detached threads.

## SEE ALSO

[futex_wake](futex_wake.md),
[handle_close](handle_close.md),
[thread_exit](thread_exit.md),
[vmar_unmap_handle_close_thread_exit](vmar_unmap_handle_close_thread_exit.md).
