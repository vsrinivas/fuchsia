# zx_thread_start

## NAME

thread_start - start execution on a thread

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_thread_start(zx_handle_t handle, zx_vaddr_t entry, zx_vaddr_t stack,
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
*ZX_THREAD_TERMINATED* when the thread stops executing (due to
*thread_exit**() being called.

*entry* shall point to a function that must call **thread_exit**() or
**futex_wake_handle_close_thread_exit**() or
**vmar_unmap_handle_close_thread_exit**() before reaching the last
instruction. Below is an example:

```
void thread_entry(uintptr_t arg1, uintptr_t arg2) __attribute__((noreturn)) {
	// do work here.

	zx_thread_exit();
}
```

Failing to call one of the exit functions before reaching the end of
the function will cause an architecture / toolchain specific exception.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**thread_start**() returns ZX_OK on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *thread* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *thread* is not a thread handle.

**ZX_ERR_ACCESS_DENIED**  The handle *thread* lacks *ZX_RIGHT_WRITE*.

**ZX_ERR_BAD_STATE**  *thread* is not ready to run or the process *thread*
is part of is no longer alive.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[thread_create](thread_create.md),
[thread_exit](thread_exit.md),
[futex_wake_handle_close_thread_exit](futex_wake_handle_close_thread_exit.md),
[vmar_unmap_handle_close_thread_exit](vmar_unmap_handle_close_thread_exit.md).
